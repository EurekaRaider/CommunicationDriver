using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace CommunicationDriver
{
    internal sealed class Arena : IDisposable
    {
        private readonly List<IntPtr> memory = new List<IntPtr>();

        internal IntPtr Allocate(int size)
        {
            var p = Marshal.AllocHGlobal(Math.Max(1, size));
            memory.Add(p);
            return p;
        }

        internal NBytesView Bytes(byte[] data)
        {
            if (data == null)
                return default(NBytesView);

            var p = Allocate(data.Length);
            Marshal.Copy(data, 0, p, data.Length);

            return new NBytesView { data = p, size = (ulong)data.Length };
        }

        internal unsafe NStringView Text(string s)
        {
            if (s == null)
                return default(NStringView);

            int size = Interop.Utf8.GetByteCount(s);
            var p = Allocate(size);

            if (size != 0)
                fixed (char* chars = s)
                    Interop.Utf8.GetBytes(chars, s.Length, (byte*)p, size);

            return new NStringView { data = p, size = (ulong)size };
        }

        internal IntPtr Struct<T>(T value)
            where T : struct
        {
            var p = Allocate(Marshal.SizeOf<T>());
            Marshal.StructureToPtr(value, p, false);
            return p;
        }

        internal IntPtr Array<T>(IList<T> values)
            where T : struct
        {
            int size = Marshal.SizeOf<T>();
            var p = Allocate(checked(size * values.Count));

            for (int i = 0; i < values.Count; ++i)
                Marshal.StructureToPtr(values[i], IntPtr.Add(p, i * size), false);

            return p;
        }

        internal NCheck Check(Check c)
        {
            var n = Interop.Sized<NCheck>();

            if (c == null)
                return n;

            n.kind = c.Kind;
            n.is_text = c.Text != null ? 1 : 0;
            n.text = Text(c.Text);
            n.bytes = Bytes(c.Bytes);

            return n;
        }

        internal NReceiveUntil Rule(ReceiveUntil r)
        {
            var n = Interop.Sized<NReceiveUntil>();

            if (r == null)
                return n;

            n.kind = r.Kind;
            n.count = r.Count;
            n.idle_ms = r.IdleMs;
            n.is_text = r.Text != null ? 1 : 0;
            n.text = Text(r.Text);
            n.bytes = Bytes(r.Data);
            n.include_delimiter = r.Include ? 1 : 0;

            return n;
        }

        internal NConnectionOptions Defaults(ConnectionOptions o)
        {
            return new NConnectionOptions
            {
                struct_size = (uint)Marshal.SizeOf<NConnectionOptions>(),
                encoding = (int)o.Encoding,
                new_line = Text(o.NewLine),
                receive_until = Rule(o.ReceiveUntil),
                connect_timeout_ms = Interop.Milliseconds(o.ConnectTimeout),
                io_timeout_ms = Interop.Milliseconds(o.IoTimeout),
                execution_timeout_ms = Interop.Milliseconds(o.ExecutionTimeout),
                close_timeout_ms = Interop.Milliseconds(o.CloseTimeout),
                max_response_bytes = o.MaxResponseBytes,
            };
        }

        internal IntPtr Options(CallOptions o, IntPtr cancel)
        {
            NCallOptions n;
            Native.cd_call_options_init(out n);

            if (o != null)
            {
                n.timeout_ms = o.Timeout.HasValue ? Interop.Milliseconds(o.Timeout.Value) : 0;
                n.encoding = o.Encoding.HasValue ? (int)o.Encoding.Value : 0;
                n.new_line = Text(o.NewLine);
                n.max_response_bytes = o.MaxResponseBytes;
                n.receive_until = Rule(o.ReceiveUntil);
                n.read_stream = (int)o.ReadStream;
                n.check_stream = (int)o.CheckStream;
            }

            n.cancellation = cancel;

            return Struct(n);
        }

        internal IntPtr Transfer(TransferOptions o, IntPtr cancel)
        {
            NTransferOptions n;
            Native.cd_transfer_options_init(out n);

            if (o != null)
            {
                n.timeout_ms = o.Timeout.HasValue ? Interop.Milliseconds(o.Timeout.Value) : 0;
                n.overwrite = o.Overwrite ? 1 : 0;
            }

            n.cancellation = cancel;

            return Struct(n);
        }

        public void Dispose()
        {
            foreach (var p in memory)
                Marshal.FreeHGlobal(p);
            memory.Clear();
        }
    }
}
