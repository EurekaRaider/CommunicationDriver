using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class CheckResult
    {
        public bool Passed { get; internal set; }

        public string MatchedText { get; internal set; }

        public byte[] MatchedBytes { get; internal set; }

        public IReadOnlyList<string> Groups { get; internal set; }

        public string FailureReason { get; internal set; }
    }
}
