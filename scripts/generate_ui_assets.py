"""Build firmware grayscale glyphs and RGBA icons. Requires Pillow and resvg-py.
Generated assets are committed; normal firmware builds need neither dependency.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import resvg_py, io
root = Path(__file__).resolve().parents[1]
chars = ''.join(chr(i) for i in range(32, 127)) + '剩余额度数据不可用实时路由等待请求今日概览费用估算缓存命中率输出速度供应商输入成功全部来源含读取暂无账号已结束失败连接断开下限其他多客户端次未计价正在响应·＋≈≥秒'
chars = ''.join(sorted(set(chars)))
lines = ['#pragma once', '#include <stdint.h>', 'struct UiGlyph { uint32_t code, offset; uint16_t w, h, advance; int16_t x, y; };', 'struct UiFont { const uint8_t *pixels; const UiGlyph *glyphs; uint16_t count, size; };']
for size in [18, 22, 28, 42, 88]:
    font = ImageFont.truetype('/System/Library/Fonts/STHeiti Medium.ttc', size)
    selected = chars if size <= 22 else ''.join(chr(i) for i in range(32, 127))
    pixels, glyphs = [], []
    for ch in selected:
        x0,y0,x1,y1 = font.getbbox(ch, anchor='ls')
        w,h = max(0,x1-x0),max(0,y1-y0)
        im=Image.new('L',(max(1,w),max(1,h)))
        ImageDraw.Draw(im).text((-x0,-y0),ch,font=font,fill=255,anchor='ls')
        glyphs.append((ord(ch),len(pixels),w,h,round(font.getlength(ch)),x0,y0))
        pixels.extend(im.getdata() if w*h else [])
    lines += [f'static const uint8_t uiPixels{size}[] = {{'+','.join(map(str,pixels))+'};', f'static const UiGlyph uiGlyphs{size}[] = {{'+','.join('{'+','.join(map(str,g))+'}' for g in glyphs)+'};',f'static const UiFont uiFont{size} = {{uiPixels{size},uiGlyphs{size},{len(glyphs)},{size}}};']
lines += ['struct UiIcon { const uint8_t *rgba; int size; };']
for name in ['magpie','chatgpt','claude','cursor','gemini']:
    for size in [32,40]:
        im=Image.open(io.BytesIO(resvg_py.svg_to_bytes(svg_path=str(root/f'src/assets/{name}.svg'),width=size*3,height=size*3))).convert('RGBA').resize((size,size),Image.Resampling.LANCZOS)
        if name == 'magpie':
            im.putdata([(157,235,98,a) for r,g,b,a in im.getdata()])
        lines += [f'static const uint8_t iconPixels_{name}{size}[] = {{'+','.join(map(str,im.tobytes()))+'};',f'static const UiIcon icon_{name}{size} = {{iconPixels_{name}{size},{size}}};']
(root/'firmware/src/ui_assets.h').write_text('\n'.join(lines)+'\n')
