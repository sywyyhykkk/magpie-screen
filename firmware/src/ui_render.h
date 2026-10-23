#pragma once
#include "ui_assets.h"
// Text and icons keep coverage until composited over the actual liquid pixel.
static uint8_t *uiAlpha;
static uint16_t blend565(uint16_t fg,uint16_t bg,int a);
static uint32_t nextCode(const char *&p) {
  uint32_t c = (uint8_t)*p++;
  if (c < 128) return c;
  int n = c < 224 ? 1 : c < 240 ? 2 : 3;
  c &= (1 << (6 - n)) - 1;
  while (n-- && *p) c = (c << 6) | ((uint8_t)*p++ & 63);
  return c;
}
static const UiGlyph *glyph(const UiFont &f, uint32_t code) {
  for (int i=0;i<f.count;i++) if(f.glyphs[i].code==code) return &f.glyphs[i];
  return nullptr;
}
static int textWidth(const String &s, const UiFont &f) {
  int w=0; const char *p=s.c_str();
  while(*p) { auto g=glyph(f,nextCode(p)); w+=g?g->advance:f.size; }
  return w;
}
static void uiPixel(Arduino_Canvas &c,int x,int y,uint16_t color,uint8_t a) {
  if(x<0||x>=480||y<0||y>=480||!a) return;
  int i=y*480+x;
  uint16_t old=c.getFramebuffer()[i];
  if(old!=0x0001) {
    int coverage=a+(uiAlpha[i]*(255-a)+127)/255;
    color=blend565(color,old,(a*255+coverage/2)/coverage);
    a=coverage;
  }
  c.getFramebuffer()[i]=color; uiAlpha[i]=a;
}
static void uiText(Arduino_Canvas &c,int x,int baseline,const String &s,const UiFont &f=uiFont22,uint16_t color=0xffff,int maxWidth=480) {
  const char *p=s.c_str(); int start=x;
  while(*p) {
    auto g=glyph(f,nextCode(p)); if(!g) continue;
    if(x+g->advance>start+maxWidth) break;
    for(int y=0;y<g->h;y++) for(int xx=0;xx<g->w;xx++)
      uiPixel(c,x+g->x+xx,baseline+g->y+y,color,f.pixels[g->offset+y*g->w+xx]);
    x+=g->advance;
  }
}
static void centered(Arduino_Canvas &c,int x,int y,const String &s,const UiFont &f=uiFont22,uint16_t color=0xffff) {
  uiText(c,x-textWidth(s,f)/2,y,s,f,color);
}
// Ink bounds exclude the font's side bearings and unequal CJK/Latin metrics.
struct InkBounds { int left=0, top=0, right=0, bottom=0; bool empty=true; };
static InkBounds inkBounds(const String &s,const UiFont &f) {
  InkBounds b; int pen=0; const char *p=s.c_str();
  while(*p) {
    auto g=glyph(f,nextCode(p)); if(!g) continue;
    for(int y=0;y<g->h;y++) for(int x=0;x<g->w;x++) {
      if(!f.pixels[g->offset+y*g->w+x]) continue;
      int xx=pen+g->x+x, yy=g->y+y;
      if(b.empty) {b.left=xx;b.right=xx+1;b.top=yy;b.bottom=yy+1;b.empty=false;}
      else {b.left=std::min(b.left,xx);b.right=std::max(b.right,xx+1);b.top=std::min(b.top,yy);b.bottom=std::max(b.bottom,yy+1);}
    }
    pen+=g->advance;
  }
  return b;
}
static void dottedCaption(Arduino_Canvas &c,int left,int centerY,const String &label,const String &window,const UiFont &f,uint16_t color,int gap,int suffixOffsetY=0) {
  auto a=inkBounds(label,f), b=inkBounds(window,f);
  constexpr int diameter=3;
  int aw=a.right-a.left;
  bool hasWindow=!b.empty;
  uiText(c,left-a.left,centerY-(a.top+a.bottom)/2,label,f,color);
  if(!hasWindow) return;
  int dot=left+aw+gap;
  // Symmetric antialiased dot, independent of font punctuation bearings.
  for(int y=-1;y<=1;y++) for(int x=0;x<diameter;x++)
    uiPixel(c,dot+x,centerY+y,color,(y!=0&&x!=1)?96:255);
  uiText(c,dot+diameter+gap-b.left,centerY-(b.top+b.bottom)/2+suffixOffsetY,window,f,color);
}
static void quotaCaption(Arduino_Canvas &c,int center,int centerY,const String &label,const String &window,uint16_t color) {
  const auto &f=uiFont22;
  auto a=inkBounds(label,f), b=inkBounds(window,f);
  constexpr int gap=12, diameter=3;
  int width=a.right-a.left+(b.empty?0:gap*2+diameter+b.right-b.left);
  // Keep the first page's established spacing and Latin optical correction.
  dottedCaption(c,center-width/2,centerY,label,window,f,color,gap,2);
}
static void icon(Arduino_Canvas &c,int x,int y,const UiIcon &im,bool white=false,bool mirror=false) {
  for(int j=0;j<im.size;j++) for(int i=0;i<im.size;i++) {
    const uint8_t *p=im.rgba+4*(j*im.size+i);
    uiPixel(c,x+(mirror?im.size-1-i:i),y+j,white?0xffff:((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3),p[3]);
  }
}
static const UiIcon &providerIcon(String name) {
  name.toLowerCase();
  return name=="claude"||name=="anthropic"?icon_claude32:name=="cursor"?icon_cursor32:name=="codex"||name=="openai"||name=="chatgpt"?icon_chatgpt32:icon_magpie32;
}
static uint16_t blend565(uint16_t fg,uint16_t bg,int a) {
  int b=255-a;
  return (((((fg>>11)&31)*a+((bg>>11)&31)*b+127)/255)<<11) |
         (((((fg>>5)&63)*a+((bg>>5)&63)*b+127)/255)<<5) |
         (((fg&31)*a+(bg&31)*b+127)/255);
}
