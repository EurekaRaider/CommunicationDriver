using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using CommunicationDriver;

class Program
{
    static void Assert(bool yes, string reason)
    {
        if (!yes)
            throw new Exception(reason);
    }

    static async Task Main()
    {
        var expected = Environment.GetEnvironmentVariable("CD_EXPECT_POINTER_BYTES");

        if (expected != null)
            Assert(IntPtr.Size == int.Parse(expected), "managed consumer process bitness");

        Console.WriteLine("METRIC managed pointer_bytes=" + IntPtr.Size);

        bool windows = RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
        string ready = windows ? "echo|set /p=READY" : "printf READY";

        using (var driver = new Driver())
        using (var shell = driver.CreateCli(new CliOptions()))
        {
            await shell.OpenAsync();

            var r = await shell.ExecuteAsync(ready, Check.Contains("READY"));
            Assert(r.Response == "READY" && r.Check.Passed, "async text without newline");

            var bytes = System.Text.Encoding.UTF8.GetBytes(ready);
            var rb = shell.Execute(
                bytes,
                Check.Contains(System.Text.Encoding.UTF8.GetBytes("READY"))
            );
            Assert(
                System.Text.Encoding.UTF8.GetString(rb.Response) == "READY" && rb.Check.Passed,
                "byte overload"
            );
            Assert(ResponseChecker.IsMatch("READY", Check.Regex("REA(DY)")), "RE2 checker");

            var empty = await shell.ExecuteAsync(windows ? "rem empty" : "true", Check.Equals(""));
            Assert(
                empty.Check.Passed && empty.Check.MatchedText == "" && empty.Check.MatchedBytes == null,
                "empty match differs from absent match"
            );

            if (!windows)
            {
                var early = await shell.ExecuteUntilAsync(
                    "printf BUSY; sleep 0.03; printf READY; sleep 0.05; printf TAIL",
                    Check.Contains("READY")
                );
                Assert(
                    early.CompletionReason == CompletionReason.Matched,
                    "until completion reason"
                );
                Assert((await shell.ExecuteAsync(ready)).Response == "READY", "cleanup isolation");
            }

            var connections = new List<Connection>();
            var items = new List<BatchItem>();

            for (int i = 0; i < 32; ++i)
            {
                var c = driver.CreateCli(new CliOptions());
                await c.OpenAsync();
                connections.Add(c);
                items.Add(BatchItem.Execute("UP" + i, c, ready, Check.Contains("READY")));
            }

            for (int round = 0; round < 10; ++round)
            {
                var pending = Batch.RunAsync(items);
                GC.Collect();
                GC.WaitForPendingFinalizers();

                var batch = await pending;
                Assert(batch.AllSucceeded && batch.Items.Count == 32, "batch under GC pressure");
            }

            foreach (var c in connections)
                c.Dispose();

            if (!windows)
            {
                using (var cancel = new CancellationTokenSource())
                {
                    var pending = shell.ExecuteUntilAsync(
                        "printf PART; sleep 1",
                        Check.Contains("NEVER"),
                        cancellationToken: cancel.Token
                    );
                    cancel.CancelAfter(30);

                    try
                    {
                        await pending;
                        throw new Exception("cancel did not throw");
                    }
                    catch (CommOperationCanceledException e)
                    {
                        Assert(
                            e.Error.Code == ErrorCode.Cancelled && e.PartialResult != null,
                            "partial cancellation result"
                        );
                    }
                }

                await shell.CloseAsync();
                await shell.OpenAsync();
            }

            await shell.CloseAsync();
        }

        Console.WriteLine("PASS managed native async, sync/byte overloads, 32 connections, GC, cancellation and cleanup");
    }
}
