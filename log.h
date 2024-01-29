#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <ctime>

void logWarningUsageCPU(double cpuUsage, const std::string& processName, int pid) {
    std::ofstream logFile("log.txt", std::ios::app);
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
        std::tm* localTime = std::localtime(&currentTime);

        char timestamp[20];
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localTime);

        logFile << "Warning!\n-----------------------------------------------------------------------------------------------\n";
        logFile << timestamp << " -> CPU Usage TOO HIGH (" << cpuUsage << "%) | process: " << processName << " | PID:" << pid << std::endl;
        logFile << "-----------------------------------------------------------------------------------------------\n\n";

        logFile.close();
    } else {
        std::cerr << "Error opening file." << std::endl;
    }
}

void logSystemStats(double totalCpuUsage, int totalProcesses, double totalMemoryUsage) {
    std::ofstream logFile("log.txt", std::ios::app);
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
        std::tm* localTime = std::localtime(&currentTime);

        char timestamp[20];
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localTime);

        logFile << "Stat\n-----------------------------------------------------------------------------------------------\n";
        logFile << timestamp
                << " -> Total CPU Usage: " << totalCpuUsage << "%, "
                << "Total Processes: " << totalProcesses << ", "
                << "Total Memory Usage: " << totalMemoryUsage << "%\n";
        logFile << "-----------------------------------------------------------------------------------------------\n\n";

        logFile.close();
    } else {
        std::cerr << "Error opening file." << std::endl;
    }
}