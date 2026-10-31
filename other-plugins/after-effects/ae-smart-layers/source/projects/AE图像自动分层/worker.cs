using System;
using System.IO;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Globalization;
using System.Diagnostics;
public static class SplitWorker {
 const int MAXSIDE=1024, MAXPARTS=40, MAXPOINTS=128;
 static int W,H,OW,OH; static double DX,DY; static string Job; static Stopwatch Clock;
 struct P { public int x,y; public P(int a,int b){x=a;y=b;} }
 class E { public int x,y,u,v,d; public bool used; public E(int a,int b,int c,int e,int f){x=a;y=b;u=c;v=e;d=f;} }
 class R { public List<int> cells=new List<int>(); public int x0=int.MaxValue,y0=int.MaxValue,x1,y1;public string kind="UNKNOWN"; }
 class Part { public List<List<P>> paths; public double feather,padding;public string kind; }
 static void Check(){if(Clock.ElapsedMilliseconds>35000||File.Exists(Path.Combine(Job,"cancel.txt")))throw new Exception("Cancelled or analysis timed out.");}
 static string F(double n){return n.ToString("0.###",CultureInfo.InvariantCulture);}
 static void Save(string body){string p=Path.Combine(Job,"result.part");File.WriteAllText(p,"AE_PARTS_V2\n"+new DirectoryInfo(Job).Name+"\n"+body,new UTF8Encoding(false));File.Move(p,Path.Combine(Job,"result.txt"));}
 public static void Run(string job){Job=job;Clock=Stopwatch.StartNew();try{Analyze();}catch(Exception e){Save("ERROR\n"+e.Message);}}
 static double BackgroundAt(double[] model,int channel,int x,int y){double u=(double)x/Math.Max(1,W-1),v=(double)y/Math.Max(1,H-1);return model[channel]*(1-u)*(1-v)+model[3+channel]*u*(1-v)+model[6+channel]*(1-u)*v+model[9+channel]*u*v;}
 static double[] SmoothBackground(byte[] pixels,List<int> border,out double noise){
  noise=0;if(W<4||H<4)return null;int[] corners={0,W-1,(H-1)*W,H*W-1};double[] model=new double[12];double spread=0;
  for(int c=0;c<3;c++){double min=255,max=0;for(int j=0;j<4;j++){int at=corners[j]*4;if(pixels[at+3]<250)return null;model[j*3+c]=pixels[at+c];min=Math.Min(min,pixels[at+c]);max=Math.Max(max,pixels[at+c]);}spread=Math.Max(spread,max-min);}if(spread<20)return null;
  List<double> errors=new List<double>();foreach(int index in border){int at=index*4;if(pixels[at+3]<250)return null;int x=index%W,y=index/W;double error=0;for(int c=0;c<3;c++)error=Math.Max(error,Math.Abs(pixels[at+c]-BackgroundAt(model,c,x,y)));errors.Add(error);}errors.Sort();
  double q95=errors[(int)((errors.Count-1)*0.95)],q99=errors[(int)((errors.Count-1)*0.99)];if(q95>4||q99>8)return null;noise=q99+1;return model;
 }
 static void Analyze(){
  Stage("LOAD_IMAGE");
  string path=File.ReadAllText(Path.Combine(Job,"source.txt"),Encoding.UTF8).TrimEnd('\r','\n');
  long length;DateTime modified;
  float[] score;
  using(Image image=OpenReadyImage(path)){
   FileInfo before=new FileInfo(path);length=before.Length;modified=before.LastWriteTimeUtc;
   OW=image.Width;OH=image.Height;if((long)OW*OH>100000000)throw new Exception("Image exceeds 100 megapixels.");
   double scale=Math.Min(1.0,(double)MAXSIDE/Math.Max(OW,OH));if(OW>OH*2.5)scale=Math.Min(scale,120.0/OH);W=Math.Max(1,(int)Math.Ceiling(OW*scale));H=Math.Max(1,(int)Math.Ceiling(OH*scale));DX=(double)OW/W;DY=(double)OH/H;
   using(Bitmap bitmap=new Bitmap(W,H,PixelFormat.Format32bppArgb)){
    using(Graphics g=Graphics.FromImage(bitmap)){g.CompositingMode=CompositingMode.SourceCopy;g.InterpolationMode=InterpolationMode.HighQualityBilinear;g.DrawImage(image,new Rectangle(0,0,W,H),0,0,OW,OH,GraphicsUnit.Pixel);}
    BitmapData data=bitmap.LockBits(new Rectangle(0,0,W,H),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
    byte[] pixels=new byte[W*H*4];try{byte[] row=new byte[W*4];for(int y=0;y<H;y++){Marshal.Copy(IntPtr.Add(data.Scan0,y*data.Stride),row,0,row.Length);Buffer.BlockCopy(row,0,pixels,y*W*4,row.Length);}}finally{bitmap.UnlockBits(data);}
    List<int> border=new List<int>();for(int x=0;x<W;x++){border.Add(x);if(H>1)border.Add((H-1)*W+x);}for(int y=1;y<H-1;y++){border.Add(y*W);if(W>1)border.Add(y*W+W-1);}
    int transparent=0,bestKey=0,bestCount=0;Dictionary<int,int> counts=new Dictionary<int,int>();
    foreach(int at in border){int k=at*4;if(pixels[k+3]<8){transparent++;continue;}int key=(pixels[k]>>3)|((pixels[k+1]>>3)<<5)|((pixels[k+2]>>3)<<10);int count;counts.TryGetValue(key,out count);counts[key]=++count;if(count>bestCount){bestCount=count;bestKey=key;}}
    double bb=0,bg=0,br=0,n=0;foreach(int at in border){int k=at*4;int key=(pixels[k]>>3)|((pixels[k+1]>>3)<<5)|((pixels[k+2]>>3)<<10);if(pixels[k+3]>=8&&key==bestKey){bb+=pixels[k];bg+=pixels[k+1];br+=pixels[k+2];n++;}}
    if(n>0){bb/=n;bg/=n;br/=n;}bool alpha=transparent>=Math.Max(1,border.Count/4);score=new float[W*H];double noise=0;double[] gradient=alpha?null:SmoothBackground(pixels,border,out noise);
    for(int y=0;y<H;y++){if((y&15)==0)Check();for(int x=0;x<W;x++){int at=y*W+x,k=at*4;double a=pixels[k+3]/255.0,contrast=gradient==null?Math.Max(Math.Abs(pixels[k]-bb),Math.Max(Math.Abs(pixels[k+1]-bg),Math.Abs(pixels[k+2]-br))):Math.Max(0,Math.Max(Math.Abs(pixels[k]-BackgroundAt(gradient,0,x,y)),Math.Max(Math.Abs(pixels[k+1]-BackgroundAt(gradient,1,x,y)),Math.Abs(pixels[k+2]-BackgroundAt(gradient,2,x,y))))-noise);score[at]=(float)(alpha?a:contrast/255.0*a);}}
   }
  }
  FileInfo after=new FileInfo(path);if(after.Length!=length||after.LastWriteTimeUtc!=modified)throw new Exception("Source image changed during analysis.");Check();
  Stage("OBJECT_SEGMENTATION");
  float[] smooth=Blur(Blur(score));float peak=0;foreach(float v in smooth)if(v>peak)peak=v;if(peak<0.008)throw new Exception("Insufficient foreground contrast.");
  double threshold=ForegroundThreshold(smooth,peak);
  double supportLevel=Math.Max(0.001,threshold*0.10);bool[] support=new bool[score.Length];for(int i=0;i<score.Length;i++)support[i]=score[i]>=supportLevel;
  // Use low-threshold candidates so a bright subject cannot erase a separate faint one.
  // Never force a split of candidates connected by blur or a weak real stroke.
  List<R> all=Components(support),regions=new List<R>();foreach(R r in all){double localPeak=0;int localCore=0;foreach(int at in r.cells)localPeak=Math.Max(localPeak,smooth[at]);foreach(int at in r.cells)if(smooth[at]>=localPeak*0.5)localCore++;if(r.cells.Count>=4&&localCore>=3&&localPeak>=0.008)regions.Add(r);}
  int retainedCells=0;foreach(R region in regions)retainedCells+=region.cells.Count;
  regions=CharacterRegions(GroupAlignedFragments(regions,smooth),smooth);
  for(int pass=0;pass<3;pass++){int before=regions.Count;regions=RefineCharacterGaps(regions,smooth);if(regions.Count==before)break;}
  regions.Sort(delegate(R a,R b){int d=a.y0-b.y0;return d==0?a.x0-b.x0:d;});
  if(regions.Count==0||regions.Count>MAXPARTS)throw new Exception("Detected "+regions.Count+" subjects; expected 1.."+MAXPARTS+".");
  ClassifyRegions(regions,smooth,false);
  regions=MergeUncertainFragments(regions,smooth);
  regions=RefitUncertainNeighborhoods(regions,smooth);
  ClassifyRegions(regions,smooth,true);
  File.WriteAllText(Path.Combine(Job,"route.txt"),"ADAPTIVE_CHARACTERS");
  File.WriteAllText(Path.Combine(Job,"threshold.txt"),"otsu="+F(threshold)+" support="+F(supportLevel));
  if(regions.Count==0||regions.Count>MAXPARTS)throw new Exception("Detected "+regions.Count+" subjects; expected 1.."+MAXPARTS+".");
  int[] ids=new int[score.Length];List<int>[] cells=new List<int>[regions.Count];int assignedCells=0;for(int r=0;r<regions.Count;r++){cells[r]=regions[r].cells;foreach(int at in cells[r]){if(ids[at]!=0)throw new Exception("Overlapping region assignment.");ids[at]=r+1;assignedCells++;}}
  if(assignedCells!=retainedCells)throw new Exception("Region coverage changed during segmentation.");File.WriteAllText(Path.Combine(Job,"coverage.txt"),retainedCells+" "+assignedCells);
  List<Part> parts=new List<Part>();int totalPoints=0,totalPaths=0;
  for(int r=0;r<regions.Count;r++){Check();Part part=new Part();part.kind=regions[r].kind;part.paths=Trace(cells[r],ids,r+1);if(part.paths.Count==0||part.paths.Count>8)throw new Exception("Outline too fragmented.");
   part.feather=Math.Max(2,Math.Min(18,Math.Max(OW,OH)*0.008));part.padding=part.feather*1.5+Math.Max(DX,DY);R a=regions[r];double nearest=1e30;
   for(int j=0;j<regions.Count;j++){if(r==j)continue;R b=regions[j];double gx=Math.Max(0,Math.Max(a.x0-b.x1-1,b.x0-a.x1-1))*DX,gy=Math.Max(0,Math.Max(a.y0-b.y1-1,b.y0-a.y1-1))*DY;nearest=Math.Min(nearest,Math.Sqrt(gx*gx+gy*gy));}
   part.feather=Math.Min(part.feather,Math.Max(1,nearest/2));part.padding=Math.Min(part.padding,Math.Max(0,nearest/4));
   foreach(List<P> poly in part.paths)totalPoints+=poly.Count;totalPaths+=part.paths.Count;parts.Add(part);
  }
  if(totalPoints>12000||totalPaths>120)throw new Exception("Outline budget exceeded.");Check();
  StringBuilder output=new StringBuilder();output.Append("OK\n").Append(OW).Append(' ').Append(OH).Append('\n').Append(parts.Count).Append('\n');
  foreach(Part p in parts){output.Append(p.kind).Append(' ').Append(F(p.feather)).Append(' ').Append(F(p.padding)).Append(' ').Append(p.paths.Count).Append('\n');foreach(List<P> poly in p.paths){output.Append(poly.Count).Append('\n');foreach(P pt in poly)output.Append(F(pt.x*DX)).Append(' ').Append(F(pt.y*DY)).Append('\n');}}
  Save(output.ToString());
 }



 static bool PngComplete(string path){
  if(!Path.GetExtension(path).Equals(".png",StringComparison.OrdinalIgnoreCase))return true;
  using(FileStream f=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)){
   if(f.Length<45)return false;
   byte[] h=new byte[8],t=new byte[12];if(f.Read(h,0,8)!=8)return false;
   f.Seek(-12,SeekOrigin.End);if(f.Read(t,0,12)!=12)return false;
   return h[0]==137&&h[1]==80&&h[2]==78&&h[3]==71&&h[4]==13&&h[5]==10&&h[6]==26&&h[7]==10&&t[4]==73&&t[5]==69&&t[6]==78&&t[7]==68;
  }
 }
 static Image OpenReadyImage(string path){
  Stage("WAIT_INPUT");Stopwatch wait=Stopwatch.StartNew();string last="input file has not appeared";
  while(wait.ElapsedMilliseconds<10000){
   Check();
   try{
    if(File.Exists(path)){
     if(PngComplete(path))return Image.FromFile(path);
     last="PNG is still being written";
    }
   }catch(OutOfMemoryException){throw;}
   catch(Exception e){last=e.Message;}
   System.Threading.Thread.Sleep(75);
  }
  throw new Exception("Input image was not ready within 10 seconds: "+last+" | "+path);
 }

 // Raster content classification, not conversion into editable AE text.
 static void ClassifyRegions(List<R> regions,float[] soft,bool finalPass){
  double[] fits=new double[regions.Count],tolerant=new double[regions.Count],compactness=new double[regions.Count];bool[] solid=new bool[regions.Count];StringBuilder report=new StringBuilder();
  for(int i=0;i<regions.Count;i++){
   Check();R r=regions[i];float[] isolated=new float[soft.Length];double peak=0;foreach(int at in r.cells){isolated[at]=soft[at];peak=Math.Max(peak,soft[at]);}
   fits[i]=GlyphTemplates.Match(isolated,W,r.x0,r.y0,r.x1,r.y1,peak);tolerant[i]=GlyphTemplates.MatchTolerant(isolated,W,r.x0,r.y0,r.x1,r.y1,peak);
   int left=W,right=0,top=H,bottom=0,area=0;foreach(int at in r.cells)if(soft[at]>=peak*0.5){int x=at%W,y=at/W;left=Math.Min(left,x);right=Math.Max(right,x);top=Math.Min(top,y);bottom=Math.Max(bottom,y);area++;}
   double hull=ConvexArea(isolated,peak*0.5,r);compactness[i]=hull>0?area/hull:0;solid[i]=compactness[i]>0.94;
  }
  for(int i=0;i<regions.Count;i++){
   R r=regions[i];int row=0;double rowFit=0;
   for(int j=0;j<regions.Count;j++){
    R b=regions[j];double h=Math.Max(r.y1-r.y0+1,b.y1-b.y0+1),centers=Math.Abs((r.y0+r.y1-b.y0-b.y1)*0.5),heightRatio=(double)Math.Min(r.y1-r.y0+1,b.y1-b.y0+1)/h;
    if(centers<h*0.25&&heightRatio>0.60&&!solid[j]&&fits[j]>0.40){row++;rowFit+=tolerant[j];}
   }
   double aspect=(double)(r.x1-r.x0+1)/(r.y1-r.y0+1);
   bool text=(!solid[i]&&((row>=2&&rowFit/row>0.78&&fits[i]>0.45&&tolerant[i]>0.76)||(fits[i]>0.68&&tolerant[i]>0.82)))||(row>=3&&rowFit/row>0.80&&solid[i]&&aspect<0.5&&tolerant[i]>0.75);
   // A lone bar or ring can also be an icon: preserve an explicit uncertain state.
   if(text&&row<2&&(solid[i]||aspect<0.18))text=false;
   r.kind=text?"TEXT":(solid[i]||(finalPass&&compactness[i]>0.85))?"OBJECT":"UNKNOWN";
   report.Append(i+1).Append(' ').Append(r.kind).Append(" box=").Append(r.x0).Append(',').Append(r.y0).Append(',').Append(r.x1).Append(',').Append(r.y1).Append(" fit=").Append(F(fits[i])).Append(" tolerant=").Append(F(tolerant[i])).Append(" row=").Append(row).Append('\n');
  }
  File.WriteAllText(Path.Combine(Job,"classification.txt"),report.ToString());
 }
 static double RegionQuality(List<R> regions,float[] soft){double sum=0,weight=0;foreach(R r in regions){Check();float[] isolated=new float[soft.Length];double peak=0;foreach(int at in r.cells){isolated[at]=soft[at];peak=Math.Max(peak,soft[at]);}double w=r.x1-r.x0+1;sum+=w*GlyphTemplates.Match(isolated,W,r.x0,r.y0,r.x1,r.y1,peak);weight+=w;}return sum/Math.Max(1,weight);}
 static List<R> RefitUncertainNeighborhoods(List<R> input,float[] soft){
  int confidentText=0;foreach(R part in input)if(part.kind=="TEXT")confidentText++;if(confidentText<2)return input;
  List<R> regions=new List<R>(input);regions.Sort(delegate(R a,R b){return a.x0-b.x0;});
  for(int end=regions.Count-1;end>=1;end--){
   if(regions[end].kind!="UNKNOWN")continue;
   for(int size=2;size<=4&&end-size+1>=0;size++){
    Check();int start=end-size+1;List<R> old=new List<R>();R joined=new R();bool valid=true;int maxHeight=0;
    for(int j=start;j<=end;j++){R part=regions[j];old.Add(part);foreach(int at in part.cells)AddCell(joined,at);maxHeight=Math.Max(maxHeight,part.y1-part.y0+1);}
    for(int j=start+1;j<=end;j++)if(regions[j].x0-regions[j-1].x1>maxHeight*0.2||Math.Abs(regions[j].y1-regions[j-1].y1)>maxHeight*0.2)valid=false;
    if(!valid)continue;List<R> context=new List<R>();for(int j=0;j<regions.Count;j++)if(j<start||j>end)context.Add(regions[j]);context.Add(joined);
    List<R> candidate=RefineCharacterGaps(context,soft),replacement=new List<R>();HashSet<int> allowed=new HashSet<int>(joined.cells);foreach(R part in candidate){bool inside=part.cells.Count>0;foreach(int at in part.cells)if(!allowed.Contains(at)){inside=false;break;}if(inside)replacement.Add(part);}
    if(replacement.Count<2||replacement.Count>=old.Count||RegionQuality(replacement,soft)<RegionQuality(old,soft)+0.05)continue;
    regions.RemoveRange(start,size);regions.InsertRange(start,replacement);end=start;break;
   }
  }
  return regions;
 }
 static List<R> MergeUncertainFragments(List<R> regions,float[] soft){
  StringBuilder log=new StringBuilder();bool changed=true;while(changed){changed=false;for(int i=0;i<regions.Count&&!changed;i++)for(int j=i+1;j<regions.Count;j++){
   Check();R a=regions[i],b=regions[j];if(a.kind!="UNKNOWN"&&b.kind!="UNKNOWN")continue;
   int height=Math.Max(a.y1-a.y0+1,b.y1-b.y0+1),gap=Math.Max(a.x0-b.x1-1,b.x0-a.x1-1);
   if(gap>height*0.12||Math.Abs(a.y1-b.y1)>height*0.25||Math.Max(a.x1,b.x1)-Math.Min(a.x0,b.x0)>height*1.8)continue;
   float[] va=new float[soft.Length],vb=new float[soft.Length],paired=new float[soft.Length];double peak=0;foreach(int at in a.cells){va[at]=paired[at]=soft[at];peak=Math.Max(peak,soft[at]);}foreach(int at in b.cells){vb[at]=paired[at]=soft[at];peak=Math.Max(peak,soft[at]);}
   double fa=GlyphTemplates.Match(va,W,a.x0,a.y0,a.x1,a.y1,peak),fb=GlyphTemplates.Match(vb,W,b.x0,b.y0,b.x1,b.y1,peak),fit=GlyphTemplates.Match(paired,W,Math.Min(a.x0,b.x0),Math.Min(a.y0,b.y0),Math.Max(a.x1,b.x1),Math.Max(a.y1,b.y1),peak);
   log.Append(a.x0).Append('+').Append(b.x0).Append(" fa=").Append(F(fa)).Append(" fb=").Append(F(fb)).Append(" pair=").Append(F(fit)).Append('\n');
   if(fit<0.55||fit<Math.Max(fa,fb)+0.04)continue;
   foreach(int at in b.cells)AddCell(a,at);a.kind="UNKNOWN";regions.RemoveAt(j);changed=true;break;
  }}File.WriteAllText(Path.Combine(Job,"fragment-merge.txt"),log.ToString());return regions;
 }
 static double Cross(P a,P b,P c){return (double)(b.x-a.x)*(c.y-a.y)-(double)(b.y-a.y)*(c.x-a.x);}
 static double ConvexArea(float[] values,double cutoff,R region){
  List<P> points=new List<P>();for(int y=region.y0;y<=region.y1;y++){int left=W,right=-1;for(int x=region.x0;x<=region.x1;x++)if(values[y*W+x]>=cutoff){left=Math.Min(left,x);right=Math.Max(right,x);}if(right>=left){points.Add(new P(left,y));points.Add(new P(left,y+1));points.Add(new P(right+1,y));points.Add(new P(right+1,y+1));}}
  if(points.Count<3)return 0;points.Sort(delegate(P a,P b){return a.x==b.x?a.y-b.y:a.x-b.x;});List<P> hull=new List<P>();
  foreach(P point in points){while(hull.Count>=2&&Cross(hull[hull.Count-2],hull[hull.Count-1],point)<=0)hull.RemoveAt(hull.Count-1);hull.Add(point);}int lower=hull.Count;
  for(int i=points.Count-2;i>=0;i--){P point=points[i];while(hull.Count>lower&&Cross(hull[hull.Count-2],hull[hull.Count-1],point)<=0)hull.RemoveAt(hull.Count-1);hull.Add(point);}double area=0;for(int i=1;i<hull.Count;i++)area+=(double)hull[i-1].x*hull[i].y-(double)hull[i].x*hull[i-1].y;return Math.Abs(area)*0.5;
 }
 // Character candidates must persist over several foreground levels. Restore halos
 // after character identities are established; low alpha contact is not glyph identity.
 static List<R> CharacterRegions(List<R> input,float[] soft){
  List<R> output=new List<R>();StringBuilder diagnostic=new StringBuilder();
  foreach(R source in input){
   Check();int rw=source.x1-source.x0+1,rh=source.y1-source.y0+1;
   diagnostic.Append("box ").Append(source.x0).Append(',').Append(source.y0).Append(' ').Append(rw).Append('x').Append(rh).Append(' ');
   if(rw<rh*0.75||rh<8){output.Add(source);continue;}
   double peak=0;foreach(int at in source.cells)peak=Math.Max(peak,soft[at]);
   List<List<R>> levels=new List<List<R>>();
   for(int step=0;step<8;step++){
    double cutoff=peak*(0.28+step*0.08);bool[] mask=new bool[soft.Length];
    foreach(int at in source.cells)if(soft[at]>=cutoff)mask[at]=true;
    List<R> candidates=Components(mask),accepted=new List<R>();
    foreach(R candidate in candidates)if(candidate.cells.Count>=Math.Max(6,source.cells.Count*0.012)&&candidate.y1-candidate.y0+1>=rh*0.52)accepted.Add(candidate);
    accepted.Sort(delegate(R a,R b){return a.x0-b.x0;});
    bool valid=accepted.Count>=2&&accepted.Count<=MAXPARTS;
    for(int j=1;j<accepted.Count;j++)if(accepted[j].x0<accepted[j-1].x1-rh*0.12)valid=false;
    levels.Add(valid?accepted:new List<R>());
    diagnostic.Append(step).Append(':').Append(valid?accepted.Count:0).Append(' ');
   }
   int best=-1,bestCount=1,bestRun=0;
   for(int i=0;i<levels.Count;i++){
    int count=levels[i].Count;if(count<2)continue;int end=i+1;
    while(end<levels.Count&&CompatibleSeeds(levels[end-1],levels[end],rh))end++;
    int run=end-i;
    if(run>=2&&(run>bestRun||(run==bestRun&&count<bestCount))){best=i+(run-1)/2;bestCount=count;bestRun=run;}
    i=end-1;
   }
   if(best<0){output.Add(source);diagnostic.Append(" -> keep\n");continue;}
   double bestQuality=SeedQuality(levels[best],soft,peak);
   for(int i=0;i<levels.Count-1;i++)if(CompatibleSeeds(levels[i],levels[i+1],rh)){
    double quality=SeedQuality(levels[i],soft,peak);if(quality>bestQuality+0.06){best=i;bestQuality=quality;}
   }
   diagnostic.Append(" quality=").Append(F(bestQuality));
   double singleFit=GlyphTemplates.Match(soft,W,source.x0,source.y0,source.x1,source.y1,peak);
   diagnostic.Append(" single=").Append(F(singleFit));
   if(singleFit>=0.70){output.Add(source);diagnostic.Append(" -> single glyph\n");continue;}
   List<R> seeds=levels[best];int[] labels=new int[soft.Length];bool[] allowed=new bool[soft.Length];Queue<int> queue=new Queue<int>();
   foreach(int at in source.cells)allowed[at]=true;
   for(int j=0;j<seeds.Count;j++)foreach(int at in seeds[j].cells){labels[at]=j+1;queue.Enqueue(at);}
   while(queue.Count>0){int at=queue.Dequeue(),x=at%W,y=at/W;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=W||ny>=H)continue;int next=ny*W+nx;if(allowed[next]&&labels[next]==0){labels[next]=labels[at];queue.Enqueue(next);}}}
   CompleteLabels(source.cells,labels,seeds.Count,true);
   // Do not sever a real interior crossbar. Edge contacts from kerning may separate.
   int[] parent=new int[seeds.Count+1];for(int j=1;j<parent.Length;j++)parent[j]=j;
   int[] crossings=new int[parent.Length*parent.Length];double[] boundaryMax=new double[crossings.Length],boundaryMass=new double[crossings.Length];
   foreach(int at in source.cells){int x=at%W;for(int d=0;d<2;d++){int next=d==0?at+1:at+W;if((d==0&&x+1>=W)||next>=labels.Length)continue;int a=labels[at],b=labels[next];if(a<=0||b<=0||a==b)continue;int key=Math.Min(a,b)*parent.Length+Math.Max(a,b);double value=Math.Min(soft[at],soft[next])/peak;boundaryMax[key]=Math.Max(boundaryMax[key],value);boundaryMass[key]+=value;}}
   for(int a=1;a<parent.Length;a++)for(int b=a+1;b<parent.Length;b++)diagnostic.Append(" B").Append(a).Append(',').Append(b).Append(" max=").Append(F(boundaryMax[a*parent.Length+b])).Append(" mass=").Append(F(boundaryMass[a*parent.Length+b]));
   foreach(int at in source.cells){int x=at%W,y=at/W;if(y<source.y0+rh*0.20||y>source.y1-rh*0.20)continue;for(int d=0;d<2;d++){int next=d==0?at+1:at+W;if((d==0&&x+1>=W)||next>=labels.Length)continue;int a=labels[at],b=labels[next];if(a>0&&b>0&&a!=b&&Math.Min(soft[at],soft[next])>peak*0.30)crossings[Math.Min(a,b)*parent.Length+Math.Max(a,b)]++;}}
   double[] partFit=new double[parent.Length];R[] grown=new R[parent.Length];for(int j=1;j<grown.Length;j++)grown[j]=new R();
   foreach(int at in source.cells)if(labels[at]>0)AddCell(grown[labels[at]],at);
   for(int j=1;j<grown.Length;j++){Check();R part=grown[j];float[] isolated=new float[soft.Length];foreach(int at in part.cells)isolated[at]=soft[at];partFit[j]=GlyphTemplates.Match(isolated,W,part.x0,part.y0,part.x1,part.y1,peak);diagnostic.Append(" fit").Append(j).Append('=').Append(F(partFit[j]));}
   for(int a=1;a<parent.Length;a++)for(int b=a+1;b<parent.Length;b++)if(crossings[a*parent.Length+b]>=2&&!(partFit[a]>=0.68&&partFit[b]>=0.68)){
    Check();float[] paired=new float[soft.Length];foreach(int at in grown[a].cells)paired[at]=soft[at];foreach(int at in grown[b].cells)paired[at]=soft[at];
    double combined=GlyphTemplates.Match(paired,W,Math.Min(grown[a].x0,grown[b].x0),Math.Min(grown[a].y0,grown[b].y0),Math.Max(grown[a].x1,grown[b].x1),Math.Max(grown[a].y1,grown[b].y1),peak);
    bool merge=combined>=(partFit[a]+partFit[b])*0.5-0.02||(seeds.Count==2&&rw<rh*1.8&&boundaryMax[a*parent.Length+b]>0.5);
    diagnostic.Append(" pair ").Append(a).Append(',').Append(b).Append(" combined=").Append(F(combined)).Append(merge?" merge":" retain");if(merge)parent[Root(parent,b)]=Root(parent,a);
   }
   Dictionary<int,R> pieces=new Dictionary<int,R>();
   foreach(int at in source.cells){int label=labels[at];if(label==0)continue;int key=Root(parent,label);R part;if(!pieces.TryGetValue(key,out part)){part=new R();pieces[key]=part;}AddCell(part,at);}
   if(pieces.Count<2)output.Add(source);else foreach(R part in pieces.Values)output.Add(part);
   diagnostic.Append(" -> ").Append(pieces.Count).Append("\n");
  }
  File.WriteAllText(Path.Combine(Job,"characters.txt"),diagnostic.ToString());return output;
 }
 // A joined serif can survive every threshold. Compare complete single-glyph shapes
 // across image-derived gap candidates instead of treating the number of CC seeds as truth.
 static List<R> RefineCharacterGaps(List<R> glyphs,float[] soft){
  int gx0=W,gy0=H,gx1=0,gy1=0;foreach(R r in glyphs){gx0=Math.Min(gx0,r.x0);gy0=Math.Min(gy0,r.y0);gx1=Math.Max(gx1,r.x1);gy1=Math.Max(gy1,r.y1);}
  if(gx1-gx0+1<(gy1-gy0+1)*2.2)return glyphs;
  List<R> result=new List<R>();StringBuilder log=new StringBuilder();
  foreach(R source in glyphs){
   Check();int width=source.x1-source.x0+1,height=source.y1-source.y0+1;
   if(width<height*1.5){result.Add(source);continue;}
   float[] isolated=new float[soft.Length];double peak=0;double[] projection=new double[width];
   foreach(int at in source.cells){isolated[at]=soft[at];peak=Math.Max(peak,soft[at]);projection[at%W-source.x0]+=soft[at];}
   double whole=GlyphTemplates.Match(isolated,W,source.x0,source.y0,source.x1,source.y1,peak);
   log.Append("box ").Append(source.x0).Append(' ').Append(width).Append('x').Append(height).Append(" single=").Append(F(whole)).Append('\n');
   if(whole>=0.70){result.Add(source);continue;}
   double[] filtered=new double[width];for(int x=0;x<width;x++){double sum=0,weight=0;for(int d=-2;d<=2;d++){int xx=x+d;if(xx>=0&&xx<width){int w=d==0?3:Math.Abs(d)==1?2:1;sum+=projection[xx]*w;weight+=w;}}filtered[x]=sum/weight;}
   List<int> boundaries=new List<int>();boundaries.Add(0);int radius=Math.Max(4,(int)(height*0.65)),minWidth=Math.Max(3,(int)(height*0.10));
   for(int x=minWidth;x<width-minWidth;x++){
    if(filtered[x]>filtered[x-1]||filtered[x]>=filtered[x+1])continue;
    double a=0,b=0;for(int j=Math.Max(0,x-radius);j<x;j++)a=Math.Max(a,filtered[j]);for(int j=x+1;j<Math.Min(width,x+radius);j++)b=Math.Max(b,filtered[j]);
    if(filtered[x]>=Math.Min(a,b)*0.85)continue;
    if(x-boundaries[boundaries.Count-1]<minWidth){if(boundaries.Count>1&&filtered[x]<filtered[boundaries[boundaries.Count-1]])boundaries[boundaries.Count-1]=x;continue;}
    boundaries.Add(x);
   }
   boundaries.Add(width);int n=boundaries.Count;log.Append(" boundaries:");foreach(int cut in boundaries)log.Append(cut).Append(',');log.Append('\n');
   if(n<3||n>24){result.Add(source);continue;}
   double[,] fits=new double[n,n];double[] best=new double[n];int[] previous=new int[n],counts=new int[n];for(int j=1;j<n;j++)best[j]=double.NegativeInfinity;
   for(int end=1;end<n;end++)for(int begin=0;begin<end;begin++){
    Check();int span=boundaries[end]-boundaries[begin];if(span<minWidth||span>height*1.9||double.IsNegativeInfinity(best[begin]))continue;
    double fit=GlyphTemplates.MatchTolerant(isolated,W,source.x0+boundaries[begin],source.y0,source.x0+boundaries[end]-1,source.y1,peak);fits[begin,end]=fit;
    if(fit<0.72)continue;double value=best[begin]+(fit-0.75)*span-(begin>0?2:0);
    if(value>best[end]){best[end]=value;previous[end]=begin;counts[end]=counts[begin]+1;}
   }
   log.Append(" result=").Append(counts[n-1]).Append(" score=").Append(best[n-1]).Append('\n');
   if(counts[n-1]<2||best[n-1]<(Math.Max(whole,0.75)-0.75)*width+width*0.04){result.Add(source);continue;}
   List<int> chosen=new List<int>();List<double> chosenFits=new List<double>();for(int at=n-1;at>0;at=previous[at]){chosen.Add(boundaries[at]);chosenFits.Add(fits[previous[at],at]);}chosen.Add(0);chosen.Reverse();chosenFits.Reverse();
   List<int[]> seams=new List<int[]>();
   for(int i=1;i<chosen.Count-1;i++){
    int center=source.x0+chosen[i],lo=Math.Max(source.x0+(chosen[i-1]+chosen[i])/2+1,center-Math.Max(3,height/8)),hi=Math.Min(source.x0+(chosen[i]+chosen[i+1])/2-1,center+Math.Max(3,height/8));
    int[] seam=FindCharacterSeam(isolated,center,lo,hi,peak);int crossings=0;
    for(int y=source.y0+height/5;y<source.y1-height/5;y++)if(Math.Min(isolated[y*W+seam[y]-1],isolated[y*W+seam[y]])>peak*0.30)crossings++;
    double pairFit=(chosenFits[i-1]+chosenFits[i])*0.5;
    if(crossings>=2&&(chosenFits[i-1]<0.75||chosenFits[i]<0.75||pairFit<whole+0.12))continue;seams.Add(seam);
   }
   if(seams.Count==0){result.Add(source);continue;}
   R[] pieces=new R[seams.Count+1];for(int i=0;i<pieces.Length;i++)pieces[i]=new R();
   foreach(int at in source.cells){int id=0;while(id<seams.Count&&at%W>=seams[id][at/W])id++;AddCell(pieces[id],at);}
   foreach(R part in pieces)if(part.cells.Count>0)result.Add(part);
   log.Append(source.x0).Append(':').Append(pieces.Length).Append(" fit=").Append(F(best[n-1])).Append('\n');
  }
  File.WriteAllText(Path.Combine(Job,"gap-refinement.txt"),log.ToString());return result;
 }
 static int[] FindCharacterSeam(float[] soft,int center,int lo,int hi,double peak){
  lo=Math.Max(1,Math.Min(lo,center));hi=Math.Min(W-1,Math.Max(hi,center));int span=hi-lo+1;
  double[] previous=new double[span],next=new double[span];short[] back=new short[span*H];
  for(int y=0;y<H;y++){if((y&31)==0)Check();for(int j=0;j<span;j++){
   int x=lo+j;double ink=(soft[y*W+x-1]+soft[y*W+x])/Math.Max(0.001,2*peak),cost=ink*ink*20+Math.Abs(x-center)*0.01;
   if(y==0){next[j]=cost;continue;}int winner=j;double best=previous[j];for(int d=-1;d<=1;d++){int k=j+d;if(k>=0&&k<span&&previous[k]+Math.Abs(d)*0.05<best){winner=k;best=previous[k]+Math.Abs(d)*0.05;}}next[j]=best+cost;back[y*span+j]=(short)(winner-j);
  }double[] swap=previous;previous=next;next=swap;}
  int last=0;for(int j=1;j<span;j++)if(previous[j]<previous[last])last=j;int[] path=new int[H];for(int y=H-1;y>=0;y--){path[y]=lo+last;if(y>0)last+=back[y*span+last];}return path;
 }
 // Disconnected accents/dots without tall seeds still belong to a character; never drop them.
 static void CompleteLabels(List<int> cells,int[] labels,int count,bool favorVerticalAlignment){
  double[] xs=new double[count+1],ys=new double[count+1];int[] sizes=new int[count+1];
  foreach(int at in cells){int id=labels[at];if(id>0){xs[id]+=at%W;ys[id]+=at/W;sizes[id]++;}}
  for(int i=1;i<=count;i++)if(sizes[i]>0){xs[i]/=sizes[i];ys[i]/=sizes[i];}
  foreach(int at in cells)if(labels[at]==0){int winner=0;double best=double.MaxValue,x=at%W,y=at/W;for(int i=1;i<=count;i++)if(sizes[i]>0){double dx=x-xs[i],dy=y-ys[i],distance=dx*dx*(favorVerticalAlignment?4:1)+dy*dy;if(distance<best){best=distance;winner=i;}}if(winner==0)throw new Exception("No valid region seeds.");labels[at]=winner;}
 }
 static double SeedQuality(List<R> seeds,float[] soft,double peak){double sum=0,weight=0;foreach(R seed in seeds){Check();double w=seed.x1-seed.x0+1;sum+=GlyphTemplates.Match(soft,W,seed.x0,seed.y0,seed.x1,seed.y1,peak)*w;weight+=w;}return sum/Math.Max(1,weight);}
 static bool CompatibleSeeds(List<R> a,List<R> b,int height){
  if(a.Count<2||a.Count!=b.Count)return false;
  for(int i=0;i<a.Count;i++){double ca=(a[i].x0+a[i].x1)*0.5,cb=(b[i].x0+b[i].x1)*0.5;if(Math.Abs(ca-cb)>height*0.12)return false;}
  return true;
 }
 static int Root(int[] parents,int at){while(parents[at]!=at){parents[at]=parents[parents[at]];at=parents[at];}return at;}
 static void AddCell(R region,int at){int x=at%W,y=at/W;region.cells.Add(at);region.x0=Math.Min(region.x0,x);region.x1=Math.Max(region.x1,x);region.y0=Math.Min(region.y0,y);region.y1=Math.Max(region.y1,y);}
 static double ForegroundThreshold(float[] values,float peak){
  // Otsu between-class variance on this image, not a screenshot-specific contrast cutoff.
  long[] histogram=new long[256];double sum=0;foreach(float value in values){int bin=Math.Max(0,Math.Min(255,(int)(value/peak*255)));histogram[bin]++;sum+=bin;}
  long count=0;double partial=0,best=-1;int first=0,last=0;
  for(int i=0;i<255;i++){count+=histogram[i];partial+=i*histogram[i];long rest=values.Length-count;if(count==0||rest==0)continue;double delta=partial/count-(sum-partial)/rest,variance=(double)count*rest*delta*delta;if(variance>best+0.0001){best=variance;first=last=i;}else if(Math.Abs(variance-best)<=0.0001)last=i;}
  return peak*Math.Max(0.08,Math.Min(0.60,((first+last)*0.5+1)/255.0));
 }
 static double Compactness(R region,float[] soft){
  float[] isolated=new float[soft.Length];double peak=0;foreach(int at in region.cells){isolated[at]=soft[at];peak=Math.Max(peak,soft[at]);}int area=0;foreach(int at in region.cells)if(soft[at]>=peak*0.5)area++;double hull=ConvexArea(isolated,peak*0.5,region);return hull>0?area/hull:0;
 }
 static List<R> GroupAlignedFragments(List<R> input,float[] soft){
  // Similar-sized, complete compact subjects are not detached glyph strokes merely because their x projections overlap.
  // Retain the small-dot/large-stem route for i/j and asymmetric detached character fragments.
  if(input.Count>400)throw new Exception("Too many foreground fragments for safe grouping.");
  List<R> result=new List<R>(input);Dictionary<R,double> compact=new Dictionary<R,double>();bool changed=true;
  while(changed){Check();changed=false;for(int i=0;i<result.Count&&!changed;i++)for(int j=i+1;j<result.Count;j++){
   if((j&31)==0)Check();R a=result[i],b=result[j];int overlap=Math.Min(a.x1,b.x1)-Math.Max(a.x0,b.x0)+1,minWidth=Math.Min(a.x1-a.x0+1,b.x1-b.x0+1),gap=Math.Max(0,Math.Max(a.y0-b.y1-1,b.y0-a.y1-1));
   int extent=Math.Max(Math.Max(a.x1-a.x0+1,b.x1-b.x0+1),Math.Max(a.y1-a.y0+1,b.y1-b.y0+1));if(overlap<Math.Max(1,minWidth*0.35)||gap>extent*1.25)continue;
   double ratio=(double)Math.Min(a.cells.Count,b.cells.Count)/Math.Max(a.cells.Count,b.cells.Count);
   if(ratio>0.35){if(!compact.ContainsKey(a))compact[a]=Compactness(a,soft);if(!compact.ContainsKey(b))compact[b]=Compactness(b,soft);if(compact[a]>0.85&&compact[b]>0.85)continue;}
   else{R large=a.cells.Count>b.cells.Count?a:b;if(!compact.ContainsKey(large))compact[large]=Compactness(large,soft);if(compact[large]>0.85&&large.x1-large.x0+1>(large.y1-large.y0+1)*0.4)continue;}
   a.cells.AddRange(b.cells);a.x0=Math.Min(a.x0,b.x0);a.y0=Math.Min(a.y0,b.y0);a.x1=Math.Max(a.x1,b.x1);a.y1=Math.Max(a.y1,b.y1);compact.Remove(a);result.RemoveAt(j);changed=true;break;
  }}return result;
 }
 static void Stage(string text){File.WriteAllText(Path.Combine(Job,"phase.txt"),text,new UTF8Encoding(false));}

 static float[] Blur(float[] a){float[] b=new float[a.Length];for(int y=0;y<H;y++){if((y&31)==0)Check();for(int x=0;x<W;x++){double sum=0,weight=0;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=W||ny>=H)continue;int w=(dx==0?2:1)*(dy==0?2:1);sum+=a[ny*W+nx]*w;weight+=w;}b[y*W+x]=(float)(sum/weight);}}return b;}
 static List<R> Components(bool[] mask){bool[] seen=new bool[mask.Length];List<R> list=new List<R>();for(int i=0;i<mask.Length;i++){if((i&8191)==0)Check();if(!mask[i]||seen[i])continue;R r=new R();Queue<int> q=new Queue<int>();seen[i]=true;q.Enqueue(i);while(q.Count>0){int at=q.Dequeue(),x=at%W,y=at/W;r.cells.Add(at);r.x0=Math.Min(r.x0,x);r.y0=Math.Min(r.y0,y);r.x1=Math.Max(r.x1,x);r.y1=Math.Max(r.y1,y);for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=W||ny>=H)continue;int ni=ny*W+nx;if(mask[ni]&&!seen[ni]){seen[ni]=true;q.Enqueue(ni);}}}list.Add(r);}return list;}
 static bool Same(int[] ids,int x,int y,int id){return x>=0&&y>=0&&x<W&&y<H&&ids[y*W+x]==id;}
 static void Edge(List<E> edges,Dictionary<int,List<int>> starts,int x,int y,int u,int v,int d){int key=y*(W+1)+x;List<int> list;if(!starts.TryGetValue(key,out list)){list=new List<int>();starts[key]=list;}list.Add(edges.Count);edges.Add(new E(x,y,u,v,d));}
 static List<List<P>> Trace(List<int> cells,int[] ids,int id){List<E> edges=new List<E>();Dictionary<int,List<int>> starts=new Dictionary<int,List<int>>();int visitedCells=0;foreach(int at in cells){if((visitedCells++&8191)==0)Check();int x=at%W,y=at/W;if(!Same(ids,x,y-1,id))Edge(edges,starts,x,y,x+1,y,0);if(!Same(ids,x+1,y,id))Edge(edges,starts,x+1,y,x+1,y+1,1);if(!Same(ids,x,y+1,id))Edge(edges,starts,x+1,y+1,x,y+1,2);if(!Same(ids,x-1,y,id))Edge(edges,starts,x,y+1,x,y,3);}
  List<List<P>> loops=new List<List<P>>();List<double> areas=new List<double>();double largest=0;
  for(int s=0;s<edges.Count;s++){if((s&8191)==0)Check();if(edges[s].used)continue;E first=edges[s];int cur=s,guard=0;bool closed=false;List<P> points=new List<P>();while(guard++<=edges.Count){E e=edges[cur];if(e.used)break;e.used=true;points.Add(new P(e.x,e.y));if(e.u==first.x&&e.v==first.y){closed=true;break;}List<int> choices;int next=-1,best=99;if(starts.TryGetValue(e.v*(W+1)+e.u,out choices))foreach(int ei in choices){E c=edges[ei];if(c.used)continue;int turn=(c.d-e.d+4)%4,rank=turn==1?0:turn==0?1:turn==3?2:3;if(rank<best){best=rank;next=ei;}}if(next<0)break;cur=next;}if(!closed)throw new Exception("Unclosed contour.");
   double area=0;List<P> simple=new List<P>();for(int k=0;k<points.Count;k++){P a=points[(k+points.Count-1)%points.Count],b=points[k],c=points[(k+1)%points.Count];if((b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x)!=0)simple.Add(b);}for(int k=0;k<simple.Count;k++){P a=simple[k],b=simple[(k+1)%simple.Count];area+=(double)a.x*b.y-(double)b.x*a.y;}
   if(area>0&&simple.Count>=3){loops.Add(simple);areas.Add(area/2);largest=Math.Max(largest,area/2);}
  }
  List<List<P>> result=new List<List<P>>();for(int i=0;i<loops.Count;i++){if(areas[i]<Math.Max(2,largest*0.002))continue;double eps=0.35;List<P> simple=Simplify(loops[i],eps);while(simple.Count>MAXPOINTS&&eps<8){eps*=1.5;simple=Simplify(loops[i],eps);}if(simple.Count>MAXPOINTS||simple.Count<3)throw new Exception("Contour complexity limit reached.");result.Add(simple);}return result;
 }
 static List<P> Simplify(List<P> p,double eps){int split=1;double far=-1;for(int i=1;i<p.Count;i++){double dx=p[i].x-p[0].x,dy=p[i].y-p[0].y,d=dx*dx+dy*dy;if(d>far){far=d;split=i;}}List<P> open=new List<P>(p);open.Add(p[0]);bool[] keep=new bool[open.Count];keep[0]=keep[split]=keep[open.Count-1]=true;Reduce(open,0,split,eps*eps,keep);Reduce(open,split,open.Count-1,eps*eps,keep);List<P> result=new List<P>();for(int i=0;i<p.Count;i++)if(keep[i])result.Add(p[i]);return result;}
 static void Reduce(List<P> p,int first,int last,double eps2,bool[] keep){Stack<int[]> stack=new Stack<int[]>();stack.Push(new int[]{first,last});while(stack.Count>0){int[] range=stack.Pop();int a=range[0],b=range[1],at=-1;double max=eps2;double vx=p[b].x-p[a].x,vy=p[b].y-p[a].y,len=vx*vx+vy*vy;for(int i=a+1;i<b;i++){double wx=p[i].x-p[a].x,wy=p[i].y-p[a].y,t=len==0?0:Math.Max(0,Math.Min(1,(wx*vx+wy*vy)/len));double dx=wx-t*vx,dy=wy-t*vy,d=dx*dx+dy*dy;if(d>max){max=d;at=i;}}if(at>=0){keep[at]=true;stack.Push(new int[]{a,at});stack.Push(new int[]{at,b});}}}
}
