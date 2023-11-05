#define EQDebug
#include "..\..\ConsoleProgram_SyncA.hpp"
#include <thread>
#include <chrono>
ConsoleProgram_SyncA program("D:\\用于被控制的控制台程序.exe");
/*测试方法：
 ----
  1

  ----
  2
  测试tes
  t
  ----
  3

  ----
  4

  ----
  5
  fuck
  test
  stop

  ----
  5
  op

  ----(命令行参数"-a-c -d f de")
  6

  ----
  7

  ----
*/
void delayedPrint() {
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	std::cout << "强制结束进程\n";
	program.Stop();
}
int main() {
	char buffer[4096];
	int i = 0;
	if (program.Start()) {
		std::cout << "程序启动成功" << std::endl;

		std::cout << "执行结束检测测试" << std::endl;
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "Output" << ++i << ": " << buffer << std::endl;
		program.Input("1\n");
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		if (program.getProcessStatus()) {
			std::cout << "进程状态：运行\n";
			program.Stop();
		} else {
			std::cout << "进程状态：已停止\n";
		}
		std::cout << "-------------\n";
		std::cout << "案例1执行完成\n";
		std::cout << "执行文本输入测试" << std::endl;
		program.Start();
		program.InputLine("2");
		program.Input("测试tes\r\nt");
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "Output" << ++i << ": " << buffer << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		if (program.getProcessStatus()) {
			std::cout << "进程状态：运行\n";
			program.Stop();
		} else {
			std::cout << "进程状态：已停止\n";
		}
		std::cout << "-------------\n";
		std::cout << "案例2执行完成\n";

		std::cout << "执行阻塞结束是否解锁测试" << std::endl;
		program.Start();
		program.InputLine("3");
		std::thread t(delayedPrint);
		t.detach();
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "Output" << ++i << ": " << buffer << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(3000));
		std::cout << "-------------\n";
		std::cout << "案例3执行完成\n";

		std::cout << "执行延时输出测试" << std::endl;
		program.Start();
		program.InputLine("4");
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "Output" << ++i << ": " << buffer << std::endl;
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "延时输出1: " << buffer << std::endl;
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "延时输出2: " << buffer << std::endl;
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "延时输出3: " << buffer << std::endl;
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "延时输出4: " << buffer << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		if (program.getProcessStatus()) {
			std::cout << "进程状态：运行\n";
		} else {
			std::cout << "进程状态：已停止\n";
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(4000));
		if (program.getProcessStatus()) {
			std::cout << "进程状态：运行\n";
			std::cout << "{不合预期！}" << std::endl;
			program.Stop();
		} else {
			std::cout << "进程状态：已停止\n";
		}
		std::cout << "-------------\n";
		std::cout << "案例4执行完成\n";

		std::cout << "执行安全退出测试" << std::endl;
		program.Start();
		program.Input("5\nfuck\ntest\n");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "Output" << ++i << ": " << buffer << std::endl;
		std::cout << "安全方法退出\n";
		if (program.Stop("stop\n", 2000)) {
			std::cout << "超时\n";
			std::cout << "{不合预期！}" << std::endl;
		} else {
			std::cout << "不超时退出\n";
		}
		std::cout << "-------------\n";
		std::cout << "案例5-1执行完成\n";

		std::cout << "执行安全退出测试2" << std::endl;
		program.Start();
		program.InputLine("5");
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		program.PullOutput(buffer, sizeof(buffer));
		std::cout << "Output" << ++i << ": " << buffer << std::endl;
		std::cout << "安全方法退出\n";
		if (program.Stop("op\n", 2000)) {
			std::cout << "超时\n";
		} else {
			std::cout << "不超时退出\n";
			std::cout << "{不合预期！}" << std::endl;
		}
		std::cout << "-------------\n";
		std::cout << "案例5-2执行完成\n";

		std::cout << "执行命令行参数测试" << std::endl;
		ConsoleProgram_SyncA* p = new ConsoleProgram_SyncA("D:\\用于被控制的控制台程序.exe", "", "-a-c -d f de");
		p->Start();
		p->InputLine("6");
		p->PullOutput(buffer, sizeof(buffer));
		std::cout << "Output" << ++i << ": " << buffer << std::endl;
		delete p;
		std::cout << "-------------\n";
		std::cout << "案例6执行完成\n";

		std::cout << "执行工作目录测试" << std::endl;
		p = new ConsoleProgram_SyncA("D:\\用于被控制的控制台程序.exe");
		p->Start();
		p->InputLine("7");
		p->PullOutput(buffer, sizeof(buffer));
		std::cout << "Output" << ++i << ": " << buffer << std::endl;
		delete p;
		std::cout << "-------------\n";
		std::cout << "案例7-1执行完成\n";
	} else {
		std::cout << "程序启动失败" << std::endl;
	}
	std::cout << "测试完成" << std::endl;
	std::this_thread::sleep_for(std::chrono::milliseconds(10000));
	return 0;
}
