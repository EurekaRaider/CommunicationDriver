using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public class CommException : Exception
    {
        public ErrorInfo Error { get; }

        public object PartialResult { get; }

        internal CommException(ErrorInfo error, object partial)
            : base(error.Message)
        {
            Error = error;
            PartialResult = partial;
        }
    }
}
