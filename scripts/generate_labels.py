"""Generate small static UI label bitmaps from the system Chinese font (build-time only)."""
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
font = ImageFont.truetype('/System/Library/Fonts/STHeiti Medium.ttc', 22)
words = ['剩余额度', '数据不可用', '实时路由', '等待请求', '今日概览', '请求', '费用估算 USD', '缓存命中率', '输出速度', '供应商']
lines = ['#pragma once', '#include <Arduino.h>', 'struct Label { const uint8_t *bits; int width; };']
for index, word in enumerate(words):
    width = int(font.getlength(word))
    image = Image.new('1', (width, 24))
    ImageDraw.Draw(image).text((0, -2), word, font=font, fill=1)
    data=[]
    for y in range(24):
        for x in range(0, width, 8):
            byte=0
            for b in range(8):
                if x+b<width and image.getpixel((x+b,y)): byte |= 128>>b
            data.append(byte)
    lines.append('static const uint8_t label%d[] PROGMEM = {%s};' % (index, ','.join(map(str,data))))
lines.append('static const Label labels[] = {' + ','.join('{label%d,%d}' % (i,int(font.getlength(w))) for i,w in enumerate(words)) + '};')
Path('firmware/src/labels.h').write_text('\n'.join(lines)+'\n')
