using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class ErrorInfo
    {
        public ErrorCode Code { get; internal set; }

        public string Message { get; internal set; }

        public string Backend { get; internal set; }

        public long NativeCode { get; internal set; }

        public string PartialText { get; internal set; }

        public byte[] PartialBytes { get; internal set; }

        public ulong? ConfirmedProgress { get; internal set; }

        public string PartialFile { get; internal set; }
    }
}
