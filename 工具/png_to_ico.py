#!/usr/bin/env python3
# png_to_ico.py
# 简单用法：python png_to_ico.py input.png output.ico

import sys
from PIL import Image, ImageFilter

DEFAULT_SIZES = [16, 24, 32, 48, 64, 128, 256]

def resize_for_icon(im, size, sharpen=False):
    # 保持 alpha，使用高质量重采样
    im2 = im.convert("RGBA")
    im_resized = im2.resize((size, size), resample=Image.LANCZOS)
    if sharpen and size <= 64:
        # 小尺寸略微锐化，增强可读性（可调整）
        im_resized = im_resized.filter(ImageFilter.UnsharpMask(radius=1, percent=120, threshold=3))
    return im_resized

def png_to_ico(input_path, output_path, sizes=None, sharpen=False):
    if sizes is None:
        sizes = DEFAULT_SIZES
    im = Image.open(input_path)
    # 如果源不是正方形，先以最大尺寸短边为基准居中裁切后缩放，避免变形
    max_size = max(sizes)
    w, h = im.size
    if w != h:
        # 中心裁剪为正方形
        min_side = min(w, h)
        left = (w - min_side) // 2
        top = (h - min_side) // 2
        im = im.crop((left, top, left + min_side, top + min_side))
    # Pillow 的 save(..., sizes=[...]) 接受列表尺寸并内置合成 ico（保留 alpha）
    # 但为了应用自定义锐化或其他处理，我们准备每个尺寸的图像
    frames = []
    for s in sorted(set(sizes)):
        frames.append(resize_for_icon(im, s, sharpen=sharpen))
    # Pillow 可以直接用 frames[0].save(..., format='ICO', sizes=[(s,s) for s in sizes])
    # 但当我们已经有 resized 图像，保存时只需要指定 sizes; Pillow 会 resample again if source != sizes,
    # 为保险起见，我们 pass the largest image and sizes, or we can compose via bytes.
    # Simpler: use the largest prepared image and let Pillow create sizes from it using high-quality resampling.
    # 找到最大尺寸的图像做为基础
    base = None
    for imf in frames:
        if imf.size[0] == max_size:
            base = imf
            break
    if base is None:
        base = frames[-1]

    # 保存 ICO，Pillow 会生成多个尺寸
    base.save(output_path, format='ICO', sizes=[(s, s) for s in sorted(set(sizes))])
    print(f"Saved {output_path} with sizes: {sorted(set(sizes))}")

def print_usage():
    print("Usage: python png_to_ico.py input.png [output.ico] [--sizes 16,32,48] [--sharpen]")
    print("Example: python png_to_ico.py logo.png app.ico --sizes 16,32,48,256 --sharpen")

def parse_args(argv):
    if len(argv) < 2:
        print_usage()
        sys.exit(1)
    input_path = argv[1]
    output_path = None
    sizes = None
    sharpen = False
    i = 2
    while i < len(argv):
        a = argv[i]
        if a == "--sizes" and i + 1 < len(argv):
            sizes = [int(x) for x in argv[i+1].split(",") if x.strip().isdigit()]
            i += 2
        elif a == "--sharpen":
            sharpen = True
            i += 1
        else:
            if output_path is None:
                output_path = a
            else:
                print(f"Unknown arg: {a}")
            i += 1
    if output_path is None:
        # derive from input
        if input_path.lower().endswith(".png"):
            output_path = input_path[:-4] + ".ico"
        else:
            output_path = input_path + ".ico"
    return input_path, output_path, sizes, sharpen

if __name__ == "__main__":
    inp, outp, sizes, sharpen = parse_args(sys.argv)
    png_to_ico(inp, outp, sizes=sizes, sharpen=sharpen)