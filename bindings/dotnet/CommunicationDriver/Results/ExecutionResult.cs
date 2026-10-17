using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class ExecutionResult<T>
    {
        public T Response { get; internal set; }

        public T StandardError { get; internal set; }

        public CheckResult Check { get; internal set; }

        public int? ExitCode { get; internal set; }

        public CompletionReason CompletionReason { get; internal set; }

        public TimeSpan Elapsed { get; internal set; }
    }
}
