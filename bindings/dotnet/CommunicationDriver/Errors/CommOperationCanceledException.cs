using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public sealed class CommOperationCanceledException : OperationCanceledException
    {
        public ErrorInfo Error { get; }

        public object PartialResult { get; }

        internal CommOperationCanceledException(ErrorInfo error, object partial)
            : base(error.Message)
        {
            Error = error;
            PartialResult = partial;
        }
    }
}
