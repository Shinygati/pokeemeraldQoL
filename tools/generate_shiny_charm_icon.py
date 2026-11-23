# Convert the supplied artwork to Emerald's 24x24, 16-color item format.
# Pillow is needed only to regenerate; normal builds use the checked-in assets.
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
im=Image.open(ROOT/'graphics/items/sources/shiny_charm.png').convert('RGBA')
im=im.crop(im.getchannel('A').getbbox())
im.thumbnail((22,22),Image.Resampling.LANCZOS)
canvas=Image.new('RGBA',(24,24));canvas.paste(im,((24-im.width)//2,(24-im.height)//2))
opaque=[(r,g,b) for r,g,b,a in canvas.getdata() if a>=96]
colors=Image.new('RGB',(len(opaque),1));colors.putdata(opaque)
quantized=colors.quantize(colors=15,method=Image.Quantize.MEDIANCUT)
palette=quantized.getpalette()[:45]
indices=iter(quantized.getdata())
output=Image.new('P',(24,24));output.putpalette([0,0,0]+palette+[0]*(765-len(palette)))
output.putdata([next(indices)+1 if alpha>=96 else 0 for alpha in canvas.getchannel('A').getdata()])
output.save(ROOT/'graphics/items/icons/shiny_charm.png',transparency=0,bits=4)
colors=[(0,0,0)]+[tuple(palette[i:i+3]) for i in range(0,45,3)]
(ROOT/'graphics/items/icon_palettes/shiny_charm.pal').write_text('JASC-PAL\n0100\n16\n'+'\n'.join(' '.join(map(str,c)) for c in colors)+'\n')
assert max(output.getdata())<=15
print('Generated 24x24 Shiny Charm icon with transparent background and 15 visible colors.')
