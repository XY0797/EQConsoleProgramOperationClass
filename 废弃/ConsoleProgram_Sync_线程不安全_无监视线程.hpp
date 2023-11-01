#include <iostream>
#include <string>
#include <windows.h>

enum class NewlineStyle {
	CR,     // \r
	LF,     // \n
	CRLF    // \r\n
};

class ConsoleProgram_Sync {
	public:

		/*
		构造时传入：文件路径 [工作目录]
		默认工作目录为文件所在目录
		*/
		ConsoleProgram_Sync(const std::string& programPath, const std::string& workingDirectory = "", const std::string& commandLineArgument = "")
			: programPath(programPath), workingDirectory(workingDirectory), commandLineArgument(commandLineArgument)
			, processExitCode(0), inputPipeRead(NULL), inputPipeWrite(NULL), outputPipeRead(NULL), outputPipeWrite(NULL), processHandle(NULL) {
			//初始化指针
			lastOutputBuffer = NULL;
			lastOutputBufferLen = 0;
			//处理工作目录
			if (workingDirectory.empty()) {
				size_t found = programPath.find_last_of("/\\");
				if (found != std::string::npos) {
					this->workingDirectory = programPath.substr(0, found);
				}
			}
#ifdef EQDebug
			std::cout << "ConsoleProgram_Sync: 构造了\n";
#endif
		}

		~ConsoleProgram_Sync() {
			Stop();
			if (lastOutputBuffer != NULL) {
				delete[] lastOutputBuffer;
			}
#ifdef EQDebug
			std::cout << "ConsoleProgram_Sync: 析构了\n";
#endif
		}

		//启动进程
		bool Start() {
			//判断进程是否启动
			if (getProcessStatus()) {
				return false;
			}
			//初始化安全标识符，使得管道可被子进程访问
			SECURITY_ATTRIBUTES securityAttributes;
			securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
			securityAttributes.bInheritHandle = TRUE;
			securityAttributes.lpSecurityDescriptor = NULL;

			//附带安全标识符创建输入管道
			if (!CreatePipe(&inputPipeRead, &inputPipeWrite, &securityAttributes, 0)) {
				//安全关闭句柄
				clhandle_s(inputPipeRead);
				clhandle_s(inputPipeWrite);
				return false;
			}

			//附带安全标识符创建输出管道
			if (!CreatePipe(&outputPipeRead, &outputPipeWrite, &securityAttributes, 0)) {
				//安全关闭句柄
				clhandle_s(inputPipeRead);
				clhandle_s(inputPipeWrite);
				clhandle_s(outputPipeRead);
				clhandle_s(outputPipeWrite);
				return false;
			}

			//初始化启动信息结构体
			STARTUPINFO startupInfo;
			ZeroMemory(&startupInfo, sizeof(startupInfo));
			startupInfo.cb = sizeof(startupInfo);

			//设置输入输出管道，设置使用自定义管道标志位
			startupInfo.hStdInput = inputPipeRead;
			startupInfo.hStdOutput = outputPipeWrite;
			startupInfo.hStdError = outputPipeWrite;
			startupInfo.dwFlags |= STARTF_USESTDHANDLES;

			//初始化进程信息结构体
			PROCESS_INFORMATION processInfo;
			ZeroMemory(&processInfo, sizeof(processInfo));

			//处理命令行信息
			std::string commandLine = "\"" + programPath + "\"";
			if (!commandLineArgument.empty()) {
				commandLine += " " + commandLineArgument;
			}
			char* commandLine_c = new char[commandLine.size() + 1];
			strncpy(commandLine_c, commandLine.c_str(), commandLine.size());
			//mbstowcs(commandLine_c, commandLine.c_str(), commandLine.size() + 1);

			//创建进程
			if (!CreateProcess(NULL, commandLine_c, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, workingDirectory.c_str(), &startupInfo, &processInfo)) {
				//释放命令行文本
				delete[] commandLine_c;

				//安全关闭句柄
				clhandle_s(inputPipeRead);
				clhandle_s(inputPipeWrite);
				clhandle_s(outputPipeRead);
				clhandle_s(outputPipeWrite);
				return false;
			}

			//释放命令行文本
			delete[] commandLine_c;

			//关闭线程句柄
			CloseHandle(processInfo.hThread);

			//保存进程句柄
			processHandle = processInfo.hProcess;

			return true;
		}

		/*
		停止进程运行，支持两种方式
		1.传入需要输入的命令(需要包含换行符)，以及非0的超时时间。
		将输入命令并且等待进程自行结束，如果超时就强制结束进程
		2.不传参数或传入其他情况的参数则强制结束进程
		*/
		void Stop(const std::string& input = "", int timeoutMilliseconds = 0) {
			//判断进程是否启动
			if (!getProcessStatus()) {
				return;
			}

			//判断参数情况
			if ( (!input.empty()) && (timeoutMilliseconds != 0) ) {
				//先尝试输入命令，等待进程自然结束
				DWORD bytesWritten;
				WriteFile(inputPipeWrite, input.c_str(), input.size(), &bytesWritten, NULL);
				DWORD waitResult = WaitForSingleObject(processHandle, timeoutMilliseconds);

				//判断是自然结束还是超时了
				if (waitResult == WAIT_TIMEOUT) {
					//超时了就强制结束进程
					TerminateProcess(processHandle, 0);
				}

				//为了防止无剩余输出导致其它线程仍在等待，这里解锁可能正在等待的线程
				UnlockOutput();

				//安全关闭句柄
				clhandle_s(inputPipeRead);
				clhandle_s(inputPipeWrite);
				clhandle_s(outputPipeRead);
				clhandle_s(outputPipeWrite);
				clhandle_s(processHandle);
			} else {
				//强制结束进程
				TerminateProcess(processHandle, 0);

				//为了防止无剩余输出导致其它线程仍在等待，这里解锁可能正在等待的线程
				UnlockOutput();

				//安全关闭句柄
				clhandle_s(inputPipeRead);
				clhandle_s(inputPipeWrite);
				clhandle_s(outputPipeRead);
				clhandle_s(outputPipeWrite);
				clhandle_s(processHandle);
			}
		}

		/*
		输入函数
		接收C风格字符串
		*/
		void Input(const char* input, DWORD len) {
			//判断进程是否启动
			if (!getProcessStatus()) {
				return;
			}

			//把文本写入输入管道
			DWORD bytesWritten;
			WriteFile(inputPipeWrite, input, len, &bytesWritten, NULL);
		}

		/*
		  输入函数
		  接收string类，原样输入，所以string需要内含换行符
		*/
		void Input(const std::string& input) {
			//判断进程是否启动
			if (!getProcessStatus()) {
				return;
			}

			//把文本写入输入管道
			DWORD bytesWritten;
			WriteFile(inputPipeWrite, input.c_str(), input.size(), &bytesWritten, NULL);
		}

		/*
		  输入一行
		  接收string类，会添加换行符，可指定换行符风格
		  换行符风格未指定时，默认采用Windows的CRLF风格
		*/
		void InputLine(const std::string& input, NewlineStyle newlineStyle = NewlineStyle::CRLF) {
			//判断进程是否启动
			if (!getProcessStatus()) {
				return;
			}

			//把文本写入输入管道
			DWORD bytesWritten;
			WriteFile(inputPipeWrite, input.c_str(), input.size(), &bytesWritten, NULL);

			//处理换行符
			const char* newline;
			switch (newlineStyle) {
				case NewlineStyle::CR:
					newline = "\r";
					break;
				case NewlineStyle::LF:
					newline = "\n";
					break;
				case NewlineStyle::CRLF:
				default:
					newline = "\r\n";
					break;
			}

			//写入换行符
			WriteFile(inputPipeWrite, newline, strlen(newline), &bytesWritten, NULL);
		}

		/*
		拉取输出
		同步方式读取输出，如果没有输出就会一直等待，直到获取到输出才返回
		返回实际写入的字节数目。如果在等待过程中进程自然结束，那么返回0
		保证在数据的末尾有\0，这个\0不计入"实际写入的字节数目"
		*/
		DWORD PullOutput(char* buffer, DWORD bufferSize) {
			//判断进程是否启动
			if (!getProcessStatus()) {
				if (lastOutputBuffer != NULL) {
					//说明有剩下的数据没有取出，这里模拟从管道取出数据
					if (lastOutputBufferLen < bufferSize) {
						//一次性搞定，直接复制进去即可
						strncpy(buffer, lastOutputBuffer, lastOutputBufferLen);
						buffer[lastOutputBufferLen] = 0;

						//删除字符串的内存空间
						delete[] lastOutputBuffer;
						lastOutputBuffer = NULL;

						return lastOutputBufferLen;
					} else {
						//需要分次拷贝

						//复制缓冲区能承受的最大长度
						strncpy(buffer, lastOutputBuffer, bufferSize - 1);
						buffer[bufferSize - 1] = 0;

						//新开辟空间存剩下的部分
						lastOutputBufferLen = lastOutputBufferLen - bufferSize + 1;
						char* tmp = new char [lastOutputBufferLen + 1];

						//复制到新空间，并且释放旧的
						strncpy(tmp, lastOutputBuffer + bufferSize - 1, lastOutputBufferLen);
						tmp[lastOutputBufferLen] = 0;
						delete[] lastOutputBuffer;
						lastOutputBuffer = tmp;

						return bufferSize - 1;
					}
				}
				return 0;
			}

			DWORD bytesRead;
#ifdef EQDebug
			std::cout << "ConsoleProgram_Sync: 读入输出\n";
#endif
			ReadFile(outputPipeRead, buffer, bufferSize - 1, &bytesRead, NULL);
#ifdef EQDebug
			std::cout << "ConsoleProgram_Sync: 读入输出完成\n";
#endif
			buffer[bytesRead] = '\0';

			return bytesRead;
		}

		//返回进程状态，正在运行返回true，否则返回false
		bool getProcessStatus() {
			UpdateProcessStatus();
			return processHandle != NULL;
		}

		//返回进程退出代码，未启动时为0，启动后和GetExitCodeProcess结果一致
		DWORD getProcessExitCode() {
			UpdateProcessStatus();
			return processExitCode;
		}

	private:
		std::string programPath;
		std::string workingDirectory;
		std::string commandLineArgument;
		DWORD processExitCode;
		HANDLE inputPipeRead;
		HANDLE inputPipeWrite;
		HANDLE outputPipeRead;
		HANDLE outputPipeWrite;
		HANDLE processHandle;
		char* lastOutputBuffer;
		DWORD lastOutputBufferLen;
		void clhandle_s(HANDLE& hd) {
			if (hd != NULL) {
				CloseHandle(hd);
				hd = NULL;
			}
		}
		void UpdateProcessStatus() {
			//判断进程是否启动
			if (processHandle == NULL) {
				return;
			}
			DWORD exitCode;
			if (GetExitCodeProcess(processHandle, &exitCode)) {
				processExitCode = exitCode;
				if (exitCode != STILL_ACTIVE) {
					//进程已结束，获取剩余输出
					getLastOutput();
					//为了防止无剩余输出导致其它线程仍在等待，这里解锁可能正在等待的线程
					UnlockOutput();
					//安全关闭句柄
					clhandle_s(inputPipeRead);
					clhandle_s(inputPipeWrite);
					clhandle_s(outputPipeRead);
					clhandle_s(outputPipeWrite);
					clhandle_s(processHandle);
					return;
				}
			} else {
				//句柄无效，关闭句柄
				//(实际上这种情况不可能发生)
				clhandle_s(processHandle);
			}
		}
		void UnlockOutput() {
			CancelIoEx(outputPipeRead, NULL);
		}
		void getLastOutput() {
			DWORD bytesRead;
			PeekNamedPipe(outputPipeRead, NULL, 0, NULL, &bytesRead, NULL);
			if (bytesRead > 0) {
				if (lastOutputBuffer != NULL) {
					delete[] lastOutputBuffer;
				}
				lastOutputBuffer = new char[bytesRead + 1];
				lastOutputBufferLen = bytesRead;
				ReadFile(outputPipeRead, lastOutputBuffer, bytesRead, &bytesRead, NULL);
				lastOutputBuffer[bytesRead] = 0;
			}
		}
};
