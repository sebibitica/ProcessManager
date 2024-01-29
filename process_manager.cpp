#include <dirent.h>
#include <sstream>
#include <pwd.h>
#include <iomanip>
#include <vector>
#include <thread>
#include <numeric>
#include <set>
#include "log.h"
#include <ncurses.h>
#include <signal.h>
#include <map>
#include <unistd.h>


struct ProcessInfo {
    int pid;
    std::string name;
    long double memory; 
    std::string user;
    double cpuUsage;
    unsigned long long readBytes;
    unsigned long long writeBytes;
    std::string bound;
    std::string cpuTime;
};

long getHertz() {
    return sysconf(_SC_CLK_TCK);
}

long getUptime() {
    std::ifstream file("/proc/uptime");
    long uptime;
    file >> uptime;
    return uptime;
}

// PROCESS STATS FUNCTIONS

// get process times stats
std::vector<long long> getProcessTimes(const std::string& pid) {
    std::ifstream file("/proc/" + pid + "/stat");
    std::string line;
    std::getline(file, line);
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    for(std::string s; iss >> s; )
        tokens.push_back(s);

    if (tokens.size() > 41) {
        // utime, stime, cutime, cstime, starttime, delayacct_blkio_ticks
        // 14, 15, 16, 17, 22, 42
        long long utime = (!tokens[13].empty()) ? std::stoll(tokens[13], nullptr, 10) : 0;
        long long stime = (!tokens[14].empty()) ? std::stoll(tokens[14], nullptr, 10) : 0;
        long long cutime = (!tokens[15].empty()) ? std::stoll(tokens[15], nullptr, 10) : 0;
        long long cstime = (!tokens[16].empty()) ? std::stoll(tokens[16], nullptr, 10) : 0;
        long long starttime = (!tokens[21].empty()) ? std::stoll(tokens[21], nullptr, 10) : 0;
        long long delayacct_blkio_ticks = (!tokens[41].empty()) ? std::stoll(tokens[41], nullptr, 10) : 0;

        return {utime, stime, cutime, cstime, starttime, delayacct_blkio_ticks};
    } else {
        return {0, 0, 0, 0, 0, 0};
    }
}


// calculate process cpu usage
double calculateProcessCPUPercentage(const std::string& pid, long uptime, long hertz) {
    // https://stackoverflow.com/questions/16726779/how-do-i-get-the-total-cpu-usage-of-an-application-from-proc-pid-stat
    // formula to calculate process cpu usage
    auto times = getProcessTimes(pid);
    long total_time = times[0] + times[1];
    total_time += times[2] + times[3];
    long seconds = uptime - (times[4] / hertz);
    if (seconds > 0) {
        return 100.0 * ((total_time / static_cast<double>(hertz)) / seconds);
    } else {
        return 0.0; 
    }
}



// classify process as I/O bound or CPU bound
std::string classifyProcess(std::vector<long long> cpuTimes) {
    // if total cpu time of process is less than io delay (ioWait), then it is I/O bound
    // else it is CPU bound
    long long totalCpuTime = cpuTimes[0] + cpuTimes[1];
    long long ioWait = cpuTimes[5];
    if (ioWait > totalCpuTime) {
        return "I/O Bound";
    } else {
        return "CPU Bound";
    }
}

// format cpu time of a process to look like m:s.s (minutes:seconds.fractionalSeconds)
std::string formatCPUTime(long long totalTicks, long hertz) {
    long secondsTotal = totalTicks / hertz; 
    int minutes = secondsTotal / 60; 
    int seconds = secondsTotal % 60;

    double fractionalSeconds = static_cast<double>(totalTicks % hertz) / hertz;
    double displaySeconds = seconds + fractionalSeconds; 

    std::ostringstream oss;
    oss << minutes << ":" << std::setfill('0') << std::setw(2) << std::fixed << std::setprecision(2) << displaySeconds;
    return oss.str();
}

// get all the info about a process
ProcessInfo getProcessInfo(int pid) {
    ProcessInfo pInfo;
    //pid of a process
    pInfo.pid = pid;

    // user id of a process
    uid_t uid;

    std::string line;
    std::ifstream statusFile("/proc/" + std::to_string(pid) + "/status");
    while (getline(statusFile, line)) {
        std::istringstream iss(line);
        std::string label;
        if (line.find("Name:") != std::string::npos) {
            iss >> label >> pInfo.name;
        } else if (line.find("VmRSS:") != std::string::npos) {
            // memory usage of a process in MB
            iss >> label >> pInfo.memory;
            pInfo.memory /= 1024;
        } else if (line.find("Uid:") != std::string::npos) {
            iss >> label >> uid;
            struct passwd *pwd = getpwuid(uid);
            if (pwd) {
                pInfo.user = pwd->pw_name;
            }
        }
    }

    // cpu usage of a process
    pInfo.cpuUsage = calculateProcessCPUPercentage(std::to_string(pid), getUptime(), getHertz());

    // cpu time of a process
    std::vector<long long> times = getProcessTimes(std::to_string(pid));
    long long totalTicks = times[0] + times[1]; 
    pInfo.cpuTime = formatCPUTime(totalTicks, getHertz());


    // read and write bytes of a process
    std::ifstream ioFile("/proc/" + std::to_string(pid) + "/io");
    while (getline(ioFile, line)) {
        std::istringstream iss(line);
        std::string label;
        if (line.find("read_bytes:") != std::string::npos) {
            iss >> label >> pInfo.readBytes;
        }
        //there are 2 lines that contain write bytes, the first one is the one we want
        // the 2nd one is cancelled_write_bytes
        else if (line.compare(0, 12, "write_bytes:") == 0) {
            iss >> label >> pInfo.writeBytes;
        }
    }

    // classify process as I/O bound or CPU bound
    pInfo.bound = classifyProcess(getProcessTimes(std::to_string(pid)));

    return pInfo;
}

// GENERAL STATS FUNCTIONS

// get number of processes
int getTotalNumberOfProcesses() {
    int count = 0;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("/proc")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (isdigit(ent->d_name[0])) {
                count++;
            }
        }
        closedir(dir);
    }
    return count;
}

// get total memory usage
double getTotalMemoryUsagePercentage() {
    std::ifstream file("/proc/meminfo");
    std::string line;
    long long totalMemory = 0, freeMemory = 0;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string label;
        long long value;
        iss >> label >> value;
        if (label == "MemTotal:") {
            totalMemory = value;
        } else if (label == "MemFree:") {
            freeMemory = value;
        }
    }

    long long usedMemory = totalMemory - freeMemory;
    return static_cast<double>(usedMemory) / totalMemory * 100.0;
}

// get cpu times stats
std::vector<unsigned long long> getTotalCPUTimes() {
    std::ifstream proc_stat("/proc/stat");
    std::string line;
    std::getline(proc_stat, line);
    std::istringstream iss(line);
    std::vector<unsigned long long> times;
    std::string cpu_label;
    unsigned long long time;
    iss >> cpu_label;
    while (iss >> time) {
        times.push_back(time);
    }
    return times;
}

// get total cpu usage
double calculateTotalCPUPercentage(const std::vector<unsigned long long>& times_start, const std::vector<unsigned long long>& times_end) {
    // using formula from https://stackoverflow.com/questions/23367857/accurate-calculation-of-cpu-usage-given-in-percentage-in-linux

    unsigned long long idle_start = times_start[3];
    unsigned long long idle_end = times_end[3];

    unsigned long long total_start = 0;
    unsigned long long total_end = 0;
    for (int i = 0; i < times_start.size(); ++i) {
        total_start += times_start[i];
        total_end += times_end[i];
    }

    double idle = idle_end - idle_start;
    double total = total_end - total_start;
    return (1.0 - idle / total) * 100.0;
}

int main() {
    initscr();
    noecho(); // don't echo input
    cbreak(); // don't wait for enter
    keypad(stdscr, TRUE); // enable arrow keys
    timeout(3000); // refresh every 3 seconds if no input


    int start_line = 0;

    int highlight = 0;
    int choice = 0;
    int c;


    double cpu_usage = 0.0;
    std::set<int> loggedProcesses;

    // log it first time (substrat 5 minutes from the current time)
    auto lastLogTime = std::chrono::system_clock::now() - std::chrono::minutes(1);
    auto lastLogWarningTime = std::chrono::system_clock::now() - std::chrono::minutes(1);

    //std::map<int, unsigned long long> prevReadBytesMap;
    //std::map<int, unsigned long long> prevWriteBytesMap;

    while (true) {
        int max_width = COLS;
        int num_lines = LINES - 7;
        std::vector<ProcessInfo> processes;
        DIR *dir;
        struct dirent *ent;
        if ((dir = opendir("/proc")) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if(isdigit(ent->d_name[0])){
                    int pid = atoi(ent->d_name);
                    if (pid > 0) {
                        processes.push_back(getProcessInfo(pid));
                    }
                }
            }
            closedir(dir);
        }

        auto times_start = getTotalCPUTimes();

        clear();

        printw("Total number of processes: %d\n", getTotalNumberOfProcesses());
        printw("                                                                             %.2f%%                %.2f%%\n\n", getTotalMemoryUsagePercentage(), cpu_usage);

        mvprintw(2, 0, "+----------+-------------------------+------------------------------+-------------------------+---------------+-----------------------+-----------------------+---------------+---------------+");
        mvprintw(3, 0, "|   PID    |          Name           |            User              |   Memory Usage(RAM)     |   CPU Usage   |       Read Bytes      |       Write Bytes     |   CPU Time    |     Bound     |");
        mvprintw(4, 0, "+----------+-------------------------+------------------------------+-------------------------+---------------+-----------------------+-----------------------+---------------+---------------+");

        int row = 5;
        for (size_t i = start_line; i < processes.size() && i < start_line + num_lines; ++i, ++row) {


            // //try to calculate the speed of read and write bytes ( and if this speed is greater than 0.5 MB/s, then the process is I/O bound )
            // if (prevReadBytesMap.find(processes[i].pid) != prevReadBytesMap.end() &&
            //     prevWriteBytesMap.find(processes[i].pid) != prevWriteBytesMap.end()) {

            //     unsigned long long readSpeed = (processes[i].readBytes - prevReadBytesMap[processes[i].pid]) / 3; // 3 seconds interval
            //     unsigned long long writeSpeed = (processes[i].writeBytes - prevWriteBytesMap[processes[i].pid]) / 3;

            //     double readSpeedMB = static_cast<double>(readSpeed) / (1024 * 1024);
            //     double writeSpeedMB = static_cast<double>(writeSpeed) / (1024 * 1024);

            //     if(readSpeedMB > 0.5 || writeSpeedMB > 0.5){
            //         processes[i].bound = "I/O Bound";
            //     }
            // }

            // //Update previous values for the next iteration
            //prevReadBytesMap[processes[i].pid] = processes[i].readBytes;
            //prevWriteBytesMap[processes[i].pid] = processes[i].writeBytes;

            std::ostringstream cpuUsageStream;
            cpuUsageStream << std::fixed << std::setprecision(2) << processes[i].cpuUsage << "%";
            std::string cpuUsageWithPercent = cpuUsageStream.str();

            std::ostringstream memoryStream;
            memoryStream << std::fixed << std::setprecision(2) << processes[i].memory << " MB";
            std::string memoryWithMB = memoryStream.str();

            // log system stats every 5 minutes
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastLogTime);

            if (elapsed.count() >= 1) {
                lastLogTime = now;
                logSystemStats(cpu_usage, getTotalNumberOfProcesses(), getTotalMemoryUsagePercentage());
            }

            // line to be displayed with info
            char line_buffer[1024];
            snprintf(line_buffer, sizeof(line_buffer), 
                "|    %-15d%-30s%-26s%-24s%-18s%-24llu%-21llu%-15s%-s   |", 
                processes[i].pid, 
                processes[i].name.c_str(),
                processes[i].user.c_str(),
                memoryWithMB.c_str(),
                cpuUsageWithPercent.c_str(),
                processes[i].readBytes,
                processes[i].writeBytes,
                processes[i].cpuTime.c_str(),
                processes[i].bound.c_str());

            std::string line = line_buffer;
            //if the line is shorter than the max width, add spaces to the end of the line to extend it to the max width of the window
            if(line.length() < max_width)
                line += std::string(max_width - line.length(), ' ');
            //if the line is longer than the max width, cut it to the max width
            else
                line = line.substr(0, max_width-1);

            // highlight selected process
            if (i == highlight) {
                attron(A_REVERSE); 
            }
            mvprintw(row, 0, "%s",line.c_str());
            if (i == highlight) {
                attroff(A_REVERSE);
            }
        }

        // clear the loggedProcesses set every 5 minutes
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastLogWarningTime);
        if (elapsed.count() >= 5) {
            loggedProcesses.clear();
            lastLogWarningTime = now;
        }

        for (size_t i = 0; i < processes.size(); ++i) {
            if (processes[i].cpuUsage > 50.0 && loggedProcesses.find(processes[i].pid) == loggedProcesses.end()) {
                loggedProcesses.insert(processes[i].pid);
                logWarningUsageCPU(processes[i].cpuUsage, processes[i].name, processes[i].pid);
            }
        }

        char infoLineBuffer[1024];
        if (choice >= 0 && choice < processes.size()) {
            snprintf(infoLineBuffer, sizeof(infoLineBuffer), "Selected Process Info: PID:%-10d Name:%-30s User:%-20s       [K] KILL       [ENTER] Select    [Q] Quit", 
            processes[choice].pid, processes[choice].name.c_str(), processes[choice].user.c_str());
        } else {
            snprintf(infoLineBuffer, sizeof(infoLineBuffer), "Selected Process Info: [K] KILL - No process selected        [ENTER] Select    [Q] Quit");
        }
        // add spaces to the end of the info line to extend it to the max width of the window
        std::string infoLine = infoLineBuffer;
        //if the line is shorter than the max width, add spaces to the end of the line to extend it to the max width of the window
        if(infoLine.length() < max_width)
            infoLine += std::string(max_width - infoLine.length(), ' ');
        //if the line is longer than the max width, cut it to the max width
        else
            infoLine = infoLine.substr(0, max_width-1);

        //info line highlighted 
        attron(A_REVERSE | A_BOLD | COLOR_PAIR(1));
        mvprintw(LINES - 1, 0 , "%s", infoLine.c_str());
        attroff(A_REVERSE | A_BOLD | COLOR_PAIR(1));

        c = getch();
        if (c != ERR) {
            switch (c) {
                case KEY_UP:
                    if (highlight > 0) {
                        --highlight;
                        if (highlight < start_line) {
                            start_line = highlight;
                        }
                    }
                    break;
                case KEY_DOWN:
                    if (highlight < processes.size() - 1) {
                        ++highlight;
                        if (highlight >= start_line + num_lines) {
                            start_line = highlight - num_lines + 1;
                        }
                    }
                    break;
                case 10: // ENTER key to select process
                    choice = highlight;
                    break;
                case 'k': // 'k' key to kill process
                    if (choice >= 0 && choice < processes.size()) {
                        if (kill(processes[choice].pid, SIGTERM) == -1) {
                            perror("Error killing process");
                        }
                    }
                    break;
                case 'q': // 'q' key to quit
                    endwin();
                    return 0;
            }
        }

        auto times_end = getTotalCPUTimes();
        cpu_usage = calculateTotalCPUPercentage(times_start, times_end);
    }

    endwin();
    return 0;
}
