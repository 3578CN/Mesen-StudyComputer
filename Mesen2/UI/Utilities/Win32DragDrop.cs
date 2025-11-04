using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

namespace Mesen.Utilities
{
    /// <summary>
    /// Windows 专用：使用 OLE/COM 启动系统级的文件拖放（支持拖到 Explorer 等外部目标）。
    /// 该实现会把要拖放的文件路径作为 CF_HDROP 数据放入 IDataObject，然后调用 DoDragDrop。
    /// </summary>
    internal static class Win32DragDrop
    {
        private const int S_OK = 0;
        private const int DRAGDROP_S_DROP = unchecked((int)0x00040100);
        private const int DRAGDROP_S_CANCEL = unchecked((int)0x00040101);
        private const int DRAGDROP_S_USEDEFAULTCURSORS = unchecked((int)0x00040102);

        private const short CF_HDROP = 15; // 标准剪贴板格式 CF_HDROP

        [DllImport("ole32.dll")]
        private static extern int OleInitialize(IntPtr pvReserved);

        [DllImport("ole32.dll")]
        private static extern void OleUninitialize();

        [DllImport("ole32.dll")]
        private static extern int DoDragDrop([MarshalAs(UnmanagedType.Interface)] IDataObject pDataObj, [MarshalAs(UnmanagedType.Interface)] IDropSource pDropSource, uint dwOKEffects, out uint pdwEffect);

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("00000121-0000-0000-C000-000000000046")]
        private interface IDropSource
        {
            [PreserveSig]
            int QueryContinueDrag([MarshalAs(UnmanagedType.Bool)] bool fEscapePressed, uint grfKeyState);
            [PreserveSig]
            int GiveFeedback(uint dwEffect);
        }

        private class DropSource : IDropSource
        {
            public int QueryContinueDrag(bool fEscapePressed, uint grfKeyState)
            {
                const uint MK_LBUTTON = 0x0001;
                if (fEscapePressed) return DRAGDROP_S_CANCEL;
                // 若左键已释放，则视为放下
                if ((grfKeyState & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
                return S_OK; // 继续
            }

            public int GiveFeedback(uint dwEffect)
            {
                // 使用系统默认的光标/外观
                return DRAGDROP_S_USEDEFAULTCURSORS;
            }
        }

        private class SimpleDataObject : IDataObject
        {
            private readonly FORMATETC[] _formats;
            private readonly STGMEDIUM _medium;

            public SimpleDataObject(IntPtr hGlobalForHDrop)
            {
                // 仅支持 CF_HDROP/TYMED_HGLOBAL
                FORMATETC fe = new FORMATETC();
                fe.cfFormat = CF_HDROP;
                fe.dwAspect = DVASPECT.DVASPECT_CONTENT;
                fe.lindex = -1;
                fe.tymed = TYMED.TYMED_HGLOBAL;
                _formats = new[] { fe };

                _medium = new STGMEDIUM();
                _medium.tymed = TYMED.TYMED_HGLOBAL;
                _medium.unionmember = hGlobalForHDrop;
                _medium.pUnkForRelease = IntPtr.Zero;
            }

            public void GetData(ref FORMATETC format, out STGMEDIUM medium)
            {
                // 尽量支持 CF_HDROP：有些外部目标在请求时可能不会把 TYMED_HGLOBAL 标志设为预期值。
                // 为提高兼容性，我们在检测到 CF_HDROP 时直接返回 HGLOBAL（_medium），而不严格依赖 format.tymed。
                if (format.cfFormat == CF_HDROP)
                {
                    medium = _medium;
                    return;
                }

                // 其它格式仍然不支持，返回 DV_E_FORMATETC
                throw new COMException("FORMAT not supported", unchecked((int)0x80040064)); // DV_E_FORMATETC
            }

            public IEnumFORMATETC EnumFormatEtc(DATADIR direction)
            {
                if (direction == DATADIR.DATADIR_GET)
                {
                    return new EnumFormatEtcImpl(_formats);
                }
                throw new NotImplementedException();
            }

            public void GetDataHere(ref FORMATETC format, ref STGMEDIUM medium)
            {
                throw new NotImplementedException();
            }

            public int QueryGetData(ref FORMATETC format)
            {
                // 只要请求的是 CF_HDROP，就返回支持（忽略请求中 tymed 的具体位），
                // 这可以避免外部目标在使用不同 tymed 标志时收到不必要的错误。
                if (format.cfFormat == CF_HDROP)
                    return S_OK;
                return unchecked((int)0x80040064); // DV_E_FORMATETC
            }

            public int GetCanonicalFormatEtc(ref FORMATETC formatIn, out FORMATETC formatOut)
            {
                // 返回相同的格式信息作为默认实现
                formatOut = formatIn;
                return S_OK;
            }

            public void SetData(ref FORMATETC formatIn, ref STGMEDIUM medium, bool release)
            {
                throw new NotImplementedException();
            }

            public IEnumSTATDATA EnumStatData()
            {
                throw new NotImplementedException();
            }

            public int DAdvise(ref FORMATETC pFormatetc, ADVF advf, IAdviseSink pAdvSink, out int pdwConnection)
            {
                pdwConnection = 0;
                return S_OK;
            }

            public void DUnadvise(int dwConnection)
            {
                // Not implemented
            }

            public int EnumDAdvise(out IEnumSTATDATA enumAdvise)
            {
                // 使用 null-forgiving 避免在开启可空引用类型检查时的警告（我们不实现此列举器）
                enumAdvise = null!;
                // Not implemented
                return S_OK;
            }

            private class EnumFormatEtcImpl : IEnumFORMATETC
            {
                private readonly FORMATETC[] _items;
                private int _idx;

                public EnumFormatEtcImpl(FORMATETC[] items)
                {
                    _items = items;
                    _idx = 0;
                }

                public int Next(int celt, FORMATETC[] rgelt, int[] pceltFetched)
                {
                    int fetched = 0;
                    while (_idx < _items.Length && fetched < celt)
                    {
                        rgelt[fetched] = _items[_idx];
                        fetched++; _idx++;
                    }
                    if (pceltFetched != null && pceltFetched.Length > 0)
                        pceltFetched[0] = fetched;
                    return (fetched == celt) ? 0 : 1; // S_OK : S_FALSE
                }

                public int Skip(int celt)
                {
                    _idx = Math.Min(_idx + celt, _items.Length);
                    return (_idx >= _items.Length) ? 1 : 0;
                }

                public int Reset()
                {
                    _idx = 0;
                    return 0;
                }

                public void Clone(out IEnumFORMATETC newEnum)
                {
                    newEnum = new EnumFormatEtcImpl(_items);
                }
            }
        }

        /// <summary>
        /// 启动系统级拖放（阻塞，直到用户放下或取消）。
        /// </summary>
        /// <param name="filePaths">要拖出的临时文件路径列表（必须存在且可访问）。</param>
        /// <returns>返回 true 表示成功触发拖放流程（最终是否被放下由系统决定）。</returns>
        public static bool DoDragDropFiles(IEnumerable<string> filePaths)
        {
            if (filePaths == null) return false;
            var files = filePaths.Where(f => !string.IsNullOrEmpty(f)).ToArray();
            if (files.Length == 0) return false;
            if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return false;

            // 创建 CF_HDROP 的 HGLOBAL
            IntPtr hGlobal = IntPtr.Zero;
            try
            {
                byte[] hdropBytes = BuildHDropData(files);
                // 分配可移动内存
                const uint GMEM_MOVEABLE = 0x0002;
                const uint GMEM_ZEROINIT = 0x0040;
                UIntPtr size = (UIntPtr)hdropBytes.Length;
                hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
                if (hGlobal == IntPtr.Zero) return false;
                IntPtr ptr = GlobalLock(hGlobal);
                if (ptr == IntPtr.Zero) { GlobalFree(hGlobal); hGlobal = IntPtr.Zero; return false; }
                Marshal.Copy(hdropBytes, 0, ptr, hdropBytes.Length);
                GlobalUnlock(hGlobal);

                // 初始化 OLE
                int oleInit = OleInitialize(IntPtr.Zero);

                var dataObj = new SimpleDataObject(hGlobal);
                var dropSrc = new DropSource();

                uint effect;
                int hr = DRAGDROP_S_CANCEL;

                // 捕获由 IDataObject/GetData 抛出的 COMException（部分接收方会探测不支持的格式并导致此异常
                // 在没有用户代码捕获的场景下会被调试器标记为 "user-unhandled"）。在此层捕获并将其视为取消拖放，
                // 可避免调试时中断，同时保持调用方逻辑安全。
                try {
                    // 使用数值 1 表示 Copy 效果，避免引用 UI 特定枚举
                    hr = DoDragDrop(dataObj, dropSrc, 1U, out effect);
                } catch( System.Runtime.InteropServices.COMException ) {
                    // 将异常视为拖放被取消或目标不支持请求的格式
                    hr = DRAGDROP_S_CANCEL;
                } finally {
                    // 如果 OleInitialize 返回 S_OK，我们应该调用 OleUninitialize
                    if (oleInit == S_OK) OleUninitialize();
                }

                return hr == S_OK || hr == DRAGDROP_S_DROP;
            }
            finally
            {
                // 注意：STGMEDIUM 的 hGlobal 应由接收方使用后释放，但如果 DoDragDrop 返回，仍需释放分配的内存
                if (hGlobal != IntPtr.Zero)
                {
                    // 等待小幅时间让系统完成使用（通常 DoDragDrop 返回后可以安全释放）
                    try { GlobalFree(hGlobal); } catch { }
                }
            }
        }

        private static byte[] BuildHDropData(string[] files)
        {
            // DROPFILES 结构：DWORD pFiles; LONG pt.x; LONG pt.y; BOOL fNC; BOOL fWide;
            // 我们使用 Unicode（fWide = 1），文件名使用双空结尾
            const int dropfilesHeaderSize = 20; // 5 * 4
            List<byte> buf = new List<byte>();
            // pFiles offset
            buf.AddRange(BitConverter.GetBytes((uint)dropfilesHeaderSize));
            // pt.x pt.y
            buf.AddRange(BitConverter.GetBytes((int)0));
            buf.AddRange(BitConverter.GetBytes((int)0));
            // fNC
            buf.AddRange(BitConverter.GetBytes((int)0));
            // fWide = TRUE
            buf.AddRange(BitConverter.GetBytes((int)1));

            // 文件名（Unicode），每个之后一个 \0，末尾再一个额外的 \0
            foreach (var f in files)
            {
                var s = f + "\0"; // single null
                var bytes = Encoding.Unicode.GetBytes(s);
                buf.AddRange(bytes);
            }
            // 额外的终止 null
            buf.AddRange(new byte[] { 0, 0 });
            return buf.ToArray();
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GlobalAlloc(uint uFlags, UIntPtr dwBytes);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GlobalLock(IntPtr hMem);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GlobalUnlock(IntPtr hMem);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GlobalFree(IntPtr hMem);
    }
}
