using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace CommunicationDriver
{
    public static class Batch
    {
        public static Task<BatchResult> RunAsync(
            IReadOnlyList<BatchItem> items,
            BatchOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            if (items == null)
                throw new ArgumentNullException(nameof(items));
            if (items.Count == 0)
                return Task.FromResult(
                    new BatchResult
                    {
                        Items = new List<BatchItemResult>().AsReadOnly(),
                        AllSucceeded = true,
                    }
                );

            var driver = items[0].Connection.Driver;
            var handles = new List<NativeHandle> { driver.Handle };

            foreach (var item in items)
            {
                if (item.Connection.Driver != driver)
                    throw new ArgumentException("All connections must belong to one Driver");
                handles.Add(item.Connection.Handle);
            }

            return Interop.Start(
                handles,
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
                    var native = new List<NBatchItem>();

                    foreach (var item in items)
                    {
                        var b = Interop.Sized<NBatchItem>();
                        b.kind = item.Kind;
                        b.key = a.Text(item.Key);
                        b.connection = item.Connection.Handle.DangerousGetHandle();
                        b.command = a.Text(item.Command);
                        b.bytes = a.Bytes(item.Bytes);
                        b.check = item.Check == null ? IntPtr.Zero : a.Struct(a.Check(item.Check));
                        b.options = a.Options(item.Options, IntPtr.Zero);
                        b.source = a.Text(item.Source);
                        b.destination = a.Text(item.Destination);
                        b.transfer_options = a.Transfer(item.TransferOptions, IntPtr.Zero);
                        native.Add(b);
                    }

                    NBatchOptions o;
                    Native.cd_batch_options_init(out o);
                    o.max_concurrency = options == null ? 32 : options.MaxConcurrency;
                    o.cancellation = cancel;

                    return Native.cd_batch_run_async(
                        h,
                        a.Array(native),
                        (ulong)native.Count,
                        ref o,
                        cb,
                        user,
                        out op
                    );
                },
                r => Interop.Batch(r)
            );
        }

        public static BatchResult Run(
            IReadOnlyList<BatchItem> items,
            BatchOptions options = null,
            CancellationToken cancellationToken = default(CancellationToken)
        )
        {
            return RunAsync(items, options, cancellationToken).GetAwaiter().GetResult();
        }
    }
}
