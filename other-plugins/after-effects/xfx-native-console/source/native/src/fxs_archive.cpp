#include "fxs_archive.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

#include "../../vendor/zlib-inflate/zlib.h"

namespace xfx {
namespace {

constexpr std::uint32_t kMaxCount = 1000000U;
constexpr std::uint32_t kMaxPathBytes = 1024U * 1024U;
constexpr std::uint32_t kMaxInflatedBytes = 256U * 1024U * 1024U;
constexpr std::size_t kMaxArchiveBytes = 512U * 1024U * 1024U;
constexpr std::size_t kMaxArchiveInflatedBytes = 512U * 1024U * 1024U;

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::uint32_t u32() {
        require(4);
        const std::uint32_t value =
            static_cast<std::uint32_t>(bytes_[position_]) |
            (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes_[position_ + 3]) << 24);
        position_ += 4;
        return value;
    }

    std::uint64_t u64() {
        require(8);
        std::uint64_t value = 0;
        for (unsigned int shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(bytes_[position_++]) << shift;
        return value;
    }

    std::string string() {
        const auto length = u32();
        if (length > kMaxPathBytes) throw std::runtime_error("FXS 路径过长。");
        require(length);
        const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(position_);
        const auto end = begin + static_cast<std::ptrdiff_t>(length);
        std::string value(begin, end);
        position_ += length;
        return value;
    }

    std::vector<std::uint8_t> bytes(std::size_t length) {
        require(length);
        const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(position_);
        const auto end = begin + static_cast<std::ptrdiff_t>(length);
        std::vector<std::uint8_t> value(begin, end);
        position_ += length;
        return value;
    }

    bool done() const { return position_ == bytes_.size(); }

private:
    void require(std::size_t count) const {
        if (position_ > bytes_.size() || count > bytes_.size() - position_)
            throw std::runtime_error("FXS 文件已截断。");
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

std::string normalize_path(const std::string& raw) {
    if (raw.empty() || raw.find('\0') != std::string::npos)
        throw std::runtime_error("FXS 包含空路径。");
    std::string path = raw;
    std::replace(path.begin(), path.end(), '/', '\\');
    if (path.front() == '\\' || path.find(':') != std::string::npos)
        throw std::runtime_error("FXS 路径必须是相对路径。");
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto end = path.find('\\', begin);
        const auto component = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (component.empty() || component == "." || component == "..")
            throw std::runtime_error("FXS 路径包含非法目录段。");
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return path;
}

std::vector<std::uint8_t> inflate_payload(const std::vector<std::uint8_t>& compressed,
                                          std::uint32_t expected_size) {
    if (expected_size > kMaxInflatedBytes) throw std::runtime_error("FXS 解压数据过大。");
    if (compressed.empty()) {
        if (expected_size != 0) throw std::runtime_error("FXS 空压缩块长度不一致。");
        return {};
    }

    // Keep one byte of output space for a zero-length stream so zlib can
    // validate the stream and report any unexpected output.
    std::vector<std::uint8_t> output(expected_size == 0 ? 1U : expected_size);
    z_stream stream{};
    const int initialized = inflateInit_(&stream, ZLIB_VERSION, static_cast<int>(sizeof(stream)));
    if (initialized != Z_OK) throw std::runtime_error("FXS zlib 初始化失败。");

    auto finish = [&]() { inflateEnd(&stream); };
    std::size_t input_offset = 0;
    std::size_t output_offset = 0;
    int result = Z_OK;
    while (result == Z_OK) {
        if (stream.avail_in == 0 && input_offset < compressed.size()) {
            const auto count = (std::min)(compressed.size() - input_offset,
                                          static_cast<std::size_t>((std::numeric_limits<uInt>::max)()));
            stream.next_in = const_cast<Bytef*>(compressed.data() + input_offset);
            stream.avail_in = static_cast<uInt>(count);
            input_offset += count;
        }
        if (stream.avail_out == 0) {
            if (output_offset == output.size()) {
                finish();
                throw std::runtime_error("FXS 解压结果超过声明长度。");
            }
            const auto count = (std::min)(output.size() - output_offset,
                                          static_cast<std::size_t>((std::numeric_limits<uInt>::max)()));
            stream.next_out = output.data() + output_offset;
            stream.avail_out = static_cast<uInt>(count);
            output_offset += count;
        }
        result = inflate(&stream, Z_NO_FLUSH);
        if (result == Z_BUF_ERROR && stream.avail_in == 0 && input_offset == compressed.size())
            break;
    }
    const auto produced = static_cast<std::size_t>(stream.total_out);
    const bool has_trailing_input =
        stream.avail_in != 0 || input_offset != compressed.size();
    finish();
    if (result != Z_STREAM_END || produced != expected_size || has_trailing_input)
        throw std::runtime_error("FXS 解压校验失败。");
    if (expected_size == 0) output.clear();
    return output;
}

void reject_duplicate(std::set<std::string>& seen, const std::string& path) {
    if (!seen.insert(path).second) throw std::runtime_error("FXS 包含重复路径。");
}

} // namespace

FxsArchive parse_fxs_bytes(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > kMaxArchiveBytes) throw std::runtime_error("FXS 文件过大。");
    Reader reader(bytes);
    if (reader.u32() != kFxsMagic) throw std::runtime_error("不是受支持的 FXS 文件。");

    FxsArchive archive;
    std::set<std::string> required_seen;
    const auto required_count = reader.u32();
    if (required_count > kMaxCount) throw std::runtime_error("FXS 基础路径数量过大。");
    archive.required_paths.reserve(required_count);
    for (std::uint32_t index = 0; index < required_count; ++index) {
        const auto path = normalize_path(reader.string());
        reject_duplicate(required_seen, path);
        archive.required_paths.push_back(path);
    }

    std::set<std::string> entry_seen;
    const auto entry_count = reader.u32();
    if (entry_count > kMaxCount) throw std::runtime_error("FXS 条目数量过大。");
    archive.entries.reserve(entry_count);
    std::size_t total_inflated = 0;
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        FxsEntry entry;
        entry.path = normalize_path(reader.string());
        reject_duplicate(entry_seen, entry.path);
        entry.flags = reader.u32();
        entry.compressed_size = reader.u32();
        entry.uncompressed_size = reader.u32();
        entry.file_time_a = reader.u64();
        entry.file_time_b = reader.u64();
        if (entry.uncompressed_size > kMaxInflatedBytes)
            throw std::runtime_error("FXS 条目解压长度过大。");
        if (entry.uncompressed_size > kMaxArchiveInflatedBytes - total_inflated)
            throw std::runtime_error("FXS 总解压长度过大。");
        entry.data = inflate_payload(reader.bytes(entry.compressed_size), entry.uncompressed_size);
        total_inflated += entry.data.size();
        archive.entries.push_back(std::move(entry));
    }
    if (!reader.done()) throw std::runtime_error("FXS 文件包含未解析的尾部数据。");
    return archive;
}

FxsArchive parse_fxs_file(const std::filesystem::path& path) {
    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(path, size_error);
    if (size_error) throw std::runtime_error("无法读取 FXS 文件大小。");
    if (file_size > kMaxArchiveBytes) throw std::runtime_error("FXS 文件过大。");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法读取 FXS 文件。");
    const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    if (input.bad()) throw std::runtime_error("读取 FXS 文件失败。");
    return parse_fxs_bytes(bytes);
}

} // namespace xfx
