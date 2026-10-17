using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class CliOptions : ConnectionOptions
    {
        public ShellKind Shell { get; set; }

        public string ExecutablePath { get; set; }

        public IList<string> Arguments { get; set; } = new List<string>();

        public string WorkingDirectory { get; set; }

        public IDictionary<string, string> Environment { get; set; } =
            new Dictionary<string, string>();

        public ExecutionMode ExecutionMode { get; set; }
    }
}
