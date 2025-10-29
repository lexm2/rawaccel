using System;

namespace userinterface.Services;

public interface IFrameTimerService
{
    void StartMonitoring(string context = "");

    void StopMonitoring(string context = "");

    void MonitorOperation(string operationName, Action operation);
}
