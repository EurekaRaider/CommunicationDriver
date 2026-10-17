using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace CommunicationDriver
{
    public sealed class Connection : IDisposable
    {
        internal readonly NativeHandle Handle;
        internal readonly Driver Driver;

        internal Connection(Driver driver, NativeHandle handle)
        {
            Driver = driver;
            Handle = handle;
        }

        public ConnectionState State
        {
            get
            {
                using (var l = new HandleLease(Handle))
                    return (ConnectionState)Native.cd_connection_get_state(l.Pointer);
            }
        }

        public Capabilities Capabilities
        {
            get
            {
                using (var l = new HandleLease(Handle))
                    return (Capabilities)Native.cd_connection_get_capabilities(l.Pointer);
            }
        }

        private Task<T> Start<T>(
            CancellationToken token,
            Interop.Submit submit,
            Func<IntPtr, T> convert
        )
        {
            return Interop.Start(new[] { Handle, Driver.Handle }, token, submit, convert);
        }

        public Task OpenAsync(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    return Native.cd_open_async(h, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return true;
                }
            );
        }

        public void Open(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            OpenAsync(options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task CloseAsync(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    return Native.cd_close_async(h, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return true;
                }
            );
        }

        public void Close(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            CloseAsync(options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task CloseInputAsync(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    return Native.cd_close_input_async(h, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return true;
                }
            );
        }

        public void CloseInput(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            CloseInputAsync(options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task<byte[]> ReadBytesAsync(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            int stream = options == null ? 0 : (int)options.ReadStream;
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    return Native.cd_read_bytes_async(h, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return Interop.Response<byte[]>(r, stream);
                }
            );
        }

        public byte[] ReadBytes(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ReadBytesAsync(options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task<string> ReadStringAsync(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            int stream = options == null ? 0 : (int)options.ReadStream;
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    return Native.cd_read_string_async(h, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return Interop.Response<string>(r, stream);
                }
            );
        }

        public string ReadString(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ReadStringAsync(options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task<string> ReadLineAsync(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            int stream = options == null ? 0 : (int)options.ReadStream;
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    return Native.cd_read_line_async(h, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return Interop.Response<string>(r, stream);
                }
            );
        }

        public string ReadLine(
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ReadLineAsync(options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task<byte[]> ReadExactlyBytesAsync(
            ulong count,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            int stream = options == null ? 0 : (int)options.ReadStream;
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    return Native.cd_read_exactly_bytes_async(h, count, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return Interop.Response<byte[]>(r, stream);
                }
            );
        }

        public byte[] ReadExactlyBytes(
            ulong count,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ReadExactlyBytesAsync(count, options, cancellationToken)
                .GetAwaiter()
                .GetResult();
        }

        public Task<string> ReadExactlyStringAsync(
            ulong count,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            int stream = options == null ? 0 : (int)options.ReadStream;
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    return Native.cd_read_exactly_string_async(h, count, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return Interop.Response<string>(r, stream);
                }
            );
        }

        public string ReadExactlyString(
            ulong count,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ReadExactlyStringAsync(count, options, cancellationToken)
                .GetAwaiter()
                .GetResult();
        }

        public Task<byte[]> ReadUntilBytesAsync(
            ReceiveUntil until,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            int stream = options == null ? 0 : (int)options.ReadStream;
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);

                    if (until == null)
                        throw new ArgumentNullException(nameof(until));

                    var rulePtr = a.Struct(a.Rule(until));
                    return Native.cd_read_until_bytes_async(h, rulePtr, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return Interop.Response<byte[]>(r, stream);
                }
            );
        }

        public byte[] ReadUntilBytes(
            ReceiveUntil until,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ReadUntilBytesAsync(until, options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task<string> ReadUntilStringAsync(
            ReceiveUntil until,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            int stream = options == null ? 0 : (int)options.ReadStream;
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);

                    if (until == null)
                        throw new ArgumentNullException(nameof(until));

                    var rulePtr = a.Struct(a.Rule(until));
                    return Native.cd_read_until_string_async(h, rulePtr, o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return Interop.Response<string>(r, stream);
                }
            );
        }

        public string ReadUntilString(
            ReceiveUntil until,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ReadUntilStringAsync(until, options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task WriteBytesAsync(
            byte[] input,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);

                    if (input == null)
                        throw new ArgumentNullException(nameof(input));

                    return Native.cd_write_bytes_async(h, a.Bytes(input), o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return true;
                }
            );
        }

        public void WriteBytes(
            byte[] input,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            WriteBytesAsync(input, options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task WriteStringAsync(
            string input,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);

                    if (input == null)
                        throw new ArgumentNullException(nameof(input));

                    return Native.cd_write_string_async(h, a.Text(input), o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return true;
                }
            );
        }

        public void WriteString(
            string input,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            WriteStringAsync(input, options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task WriteLineAsync(
            string input,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);

                    if (input == null)
                        throw new ArgumentNullException(nameof(input));

                    return Native.cd_write_line_async(h, a.Text(input), o, cb, user, out op);
                },
                r =>
                {
                    Interop.Checked(Native.cd_result_get_status(r), r);
                    return true;
                }
            );
        }

        public void WriteLine(
            string input,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            WriteLineAsync(input, options, cancellationToken).GetAwaiter().GetResult();
        }

        public Task<ExecutionResult<string>> ExecuteAsync(
            string command,
            Check check = null,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    var checkPtr = check == null ? IntPtr.Zero : a.Struct(a.Check(check));

                    if (command == null)
                        throw new ArgumentNullException(nameof(command));

                    return Native.cd_execute_string_async(
                        h,
                        a.Text(command),
                        checkPtr,
                        o,
                        cb,
                        user,
                        out op
                    );
                },
                r => Interop.Execution<string>(r)
            );
        }

        public ExecutionResult<string> Execute(
            string command,
            Check check = null,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ExecuteAsync(command, check, options, cancellationToken)
                .GetAwaiter()
                .GetResult();
        }

        public Task<ExecutionResult<string>> ExecuteUntilAsync(
            string command,
            Check match,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);

                    if (match == null)
                        throw new ArgumentNullException(nameof(match));

                    var matchPtr = a.Struct(a.Check(match));

                    if (command == null)
                        throw new ArgumentNullException(nameof(command));

                    return Native.cd_execute_until_string_async(
                        h,
                        a.Text(command),
                        matchPtr,
                        o,
                        cb,
                        user,
                        out op
                    );
                },
                r => Interop.Execution<string>(r)
            );
        }

        public ExecutionResult<string> ExecuteUntil(
            string command,
            Check match,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ExecuteUntilAsync(command, match, options, cancellationToken)
                .GetAwaiter()
                .GetResult();
        }

        public Task<ExecutionResult<byte[]>> ExecuteAsync(
            byte[] command,
            Check check = null,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);
                    var checkPtr = check == null ? IntPtr.Zero : a.Struct(a.Check(check));

                    if (command == null)
                        throw new ArgumentNullException(nameof(command));

                    return Native.cd_execute_bytes_async(
                        h,
                        a.Bytes(command),
                        checkPtr,
                        o,
                        cb,
                        user,
                        out op
                    );
                },
                r => Interop.Execution<byte[]>(r)
            );
        }

        public ExecutionResult<byte[]> Execute(
            byte[] command,
            Check check = null,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ExecuteAsync(command, check, options, cancellationToken)
                .GetAwaiter()
                .GetResult();
        }

        public Task<ExecutionResult<byte[]>> ExecuteUntilAsync(
            byte[] command,
            Check match,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Options(options, cancel);

                    if (match == null)
                        throw new ArgumentNullException(nameof(match));

                    var matchPtr = a.Struct(a.Check(match));

                    if (command == null)
                        throw new ArgumentNullException(nameof(command));

                    return Native.cd_execute_until_bytes_async(
                        h,
                        a.Bytes(command),
                        matchPtr,
                        o,
                        cb,
                        user,
                        out op
                    );
                },
                r => Interop.Execution<byte[]>(r)
            );
        }

        public ExecutionResult<byte[]> ExecuteUntil(
            byte[] command,
            Check match,
            CallOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return ExecuteUntilAsync(command, match, options, cancellationToken)
                .GetAwaiter()
                .GetResult();
        }

        public Task<TransferResult> UploadAsync(
            string source,
            string destination,
            TransferOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Transfer(options, cancel);
                    return Native.cd_upload_async(
                        h,
                        a.Text(source),
                        a.Text(destination),
                        o,
                        cb,
                        user,
                        out op
                    );
                },
                r => Interop.Transfer(r)
            );
        }

        public TransferResult Upload(
            string source,
            string destination,
            TransferOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return UploadAsync(source, destination, options, cancellationToken)
                .GetAwaiter()
                .GetResult();
        }

        public Task<TransferResult> DownloadAsync(
            string source,
            string destination,
            TransferOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return Start(
                cancellationToken,
                (
                    Arena a,
                    IntPtr h,
                    IntPtr cancel,
                    Native.Completion cb,
                    IntPtr user,
                    out IntPtr op
                ) =>
                {
                    var o = a.Transfer(options, cancel);
                    return Native.cd_download_async(
                        h,
                        a.Text(source),
                        a.Text(destination),
                        o,
                        cb,
                        user,
                        out op
                    );
                },
                r => Interop.Transfer(r)
            );
        }

        public TransferResult Download(
            string source,
            string destination,
            TransferOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return DownloadAsync(source, destination, options, cancellationToken)
                .GetAwaiter()
                .GetResult();
        }

        public void Dispose()
        {
            if (Handle.IsClosed)
                return;
            try
            {
                Close();
            }
            catch (ObjectDisposedException) when (Handle.IsClosed || Driver.Handle.IsClosed) { }
            finally
            {
                Handle.Dispose();
            }
        }
    }
}
