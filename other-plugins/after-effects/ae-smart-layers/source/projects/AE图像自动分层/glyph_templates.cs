// Lightweight single-glyph shape veto using Windows' installed generic font families.
// This is not full OCR: it prevents a convincing existing glyph from being split by weak necks.
public static class GlyphTemplates {
 const int NW=36,NH=48;
 class Template { public ulong[] bits,expanded;public int area;public double aspect; }
 static List<Template> templates;
 static object gate=new object();
 static List<Template> Templates(){
  lock(gate){
   if(templates!=null)return templates;
   templates=new List<Template>();
   FontFamily[] families={FontFamily.GenericSansSerif,FontFamily.GenericSerif,FontFamily.GenericMonospace};
   FontStyle[] styles={FontStyle.Regular,FontStyle.Bold,FontStyle.Italic};
   string alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
   foreach(FontFamily family in families)foreach(FontStyle style in styles){
    if(!family.IsStyleAvailable(style))continue;
    using(Font font=new Font(family,72,style,GraphicsUnit.Pixel))foreach(char letter in alphabet){
     using(Bitmap bitmap=new Bitmap(160,130,PixelFormat.Format32bppArgb)){
      using(Graphics graphics=Graphics.FromImage(bitmap)){
       graphics.Clear(Color.Transparent);graphics.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
       graphics.DrawString(letter.ToString(),font,Brushes.White,new PointF(8,8),StringFormat.GenericTypographic);
      }
      BitmapData data=bitmap.LockBits(new Rectangle(0,0,bitmap.Width,bitmap.Height),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
      float[] values=new float[bitmap.Width*bitmap.Height];
      try{byte[] row=new byte[bitmap.Width*4];for(int y=0;y<bitmap.Height;y++){Marshal.Copy(IntPtr.Add(data.Scan0,y*data.Stride),row,0,row.Length);for(int x=0;x<bitmap.Width;x++)values[y*bitmap.Width+x]=row[x*4+3]/255.0f;}}finally{bitmap.UnlockBits(data);}
      double aspect;bool[] mask=Normalize(values,bitmap.Width,0,0,bitmap.Width-1,bitmap.Height-1,0.5,out aspect);
      if(mask!=null){int area=0;foreach(bool value in mask)if(value)area++;templates.Add(new Template{bits=Pack(mask),expanded=Pack(Expand(mask)),area=area,aspect=aspect});}
     }
    }
   }
   return templates;
  }
 }
 static bool[] Normalize(float[] values,int width,int x0,int y0,int x1,int y1,double level,out double aspect){
  int l=x1,r=x0,t=y1,b=y0;
  for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)if(values[y*width+x]>=level){l=Math.Min(l,x);r=Math.Max(r,x);t=Math.Min(t,y);b=Math.Max(b,y);}
  if(l>r||t>b){aspect=0;return null;}
  int w=r-l+1,h=b-t+1;aspect=(double)w/h;bool[] normalized=new bool[NW*NH];
  for(int y=0;y<NH;y++)for(int x=0;x<NW;x++){
   int xx=l+Math.Min(w-1,(int)((x+0.5)*w/NW)),yy=t+Math.Min(h-1,(int)((y+0.5)*h/NH));
   normalized[y*NW+x]=values[yy*width+xx]>=level;
  }
  return normalized;
 }
 static bool[] Expand(bool[] mask){bool[] result=new bool[mask.Length];for(int y=0;y<NH;y++)for(int x=0;x<NW;x++){bool value=false;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int xx=x+dx,yy=y+dy;if(xx>=0&&xx<NW&&yy>=0&&yy<NH)value|=mask[yy*NW+xx];}result[y*NW+x]=value;}return result;}
 static ulong[] Pack(bool[] mask){ulong[] words=new ulong[(mask.Length+63)/64];for(int i=0;i<mask.Length;i++)if(mask[i])words[i>>6]|=1UL<<(i&63);return words;}
 static int PopCount(ulong value){value-=(value>>1)&0x5555555555555555UL;value=(value&0x3333333333333333UL)+((value>>2)&0x3333333333333333UL);value=(value+(value>>4))&0x0F0F0F0F0F0F0F0FUL;return (int)((value*0x0101010101010101UL)>>56);}
 public static double Match(float[] values,int width,int x0,int y0,int x1,int y1,double peak){return MatchCore(values,width,x0,y0,x1,y1,peak,false);}
 public static double MatchTolerant(float[] values,int width,int x0,int y0,int x1,int y1,double peak){return MatchCore(values,width,x0,y0,x1,y1,peak,true);}
 static double MatchCore(float[] values,int width,int x0,int y0,int x1,int y1,double peak,bool tolerant){
  double best=0;List<Template> models=Templates();
  foreach(double ratio in new double[]{0.25,0.4,0.55}){
   double aspect;bool[] sample=Normalize(values,width,x0,y0,x1,y1,peak*ratio,out aspect);if(sample==null)continue;
   ulong[] sampleBits=Pack(sample),sampleExpanded=tolerant?Pack(Expand(sample)):null;int sampleArea=0;foreach(bool value in sample)if(value)sampleArea++;
   foreach(Template model in models){
    double proportion=Math.Min(aspect,model.aspect)/Math.Max(aspect,model.aspect);if(proportion<0.80)continue;
    int intersection=0,union=0;
    for(int i=0;i<sampleBits.Length;i++){if(tolerant){intersection+=PopCount(sampleBits[i]&model.expanded[i])+PopCount(model.bits[i]&sampleExpanded[i]);}else{union+=PopCount(sampleBits[i]|model.bits[i]);intersection+=PopCount(sampleBits[i]&model.bits[i]);}}
    double fit=(double)intersection/Math.Max(1,tolerant?sampleArea+model.area:union)*proportion;if(fit>best)best=fit;
   }
  }
  return best;
 }
}
