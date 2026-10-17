using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class BatchResult
    {
        public IReadOnlyList<BatchItemResult> Items { get; internal set; }

        public bool AllSucceeded { get; internal set; }

        public bool WasCancelled { get; internal set; }

        public TimeSpan Elapsed { get; internal set; }
    }
}
