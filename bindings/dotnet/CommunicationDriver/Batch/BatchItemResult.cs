using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class BatchItemResult
    {
        public string Key { get; internal set; }

        public BatchItemState State { get; internal set; }

        public bool Succeeded { get; internal set; }

        public ExecutionResult<string> TextExecution { get; internal set; }

        public ExecutionResult<byte[]> ByteExecution { get; internal set; }

        public TransferResult Transfer { get; internal set; }

        public ErrorInfo Error { get; internal set; }

        public TimeSpan Elapsed { get; internal set; }
    }
}
