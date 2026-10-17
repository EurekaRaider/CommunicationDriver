using System;
using System.Collections.Generic;
using System.Threading;

namespace CommunicationDriver
{
    public enum ShellKind
    {
        Default,
        Cmd,
        WindowsPowerShell,
        Pwsh,
        Sh,
        Bash,
        Zsh,
    }
}
