#include <iostream>
#include <shared_mutex>
#include <thread>
#include <chrono>
#include <string>
#include <windows.h>

//换行符风格定义
enum class NewlineStyle {
	CR,     // \r
	LF,     // \n
	CRLF    // \r\n
};

//安全关闭句柄
void Clhandle_s(HANDLE& hd) {
	if (hd != NULL) {
		CloseHandle(hd);
		hd = NULL;
	}
}

//控制台程序操作类，同步方式，线程安全
//调用Stop可能抛出int型异常，值为1，表示调用结束进程后等待了2分钟，进程仍然处于运行状态
class ConsoleProgram_Sync {
	private:
		//可执行文件路径、工作目录、命令行参数
		std::string m_programPath;
		std::string m_workingDirectory;
		std::string m_commandLineArgument;

		//进程信息读写锁，多线程访问时的线程安全
		std::shared_mutex m_rwProcMutex;

		std::mutex m_outputMutex;

		//线程对象
		std::thread m_thread;

		//正在析构标志，让线程得以退出
		bool m_isExit;

		//进程状态
		bool m_processStatus;

		//进程退出代码
		DWORD m_processExitCode;

		//进程句柄
		HANDLE m_processHandle;

		//管道句柄
		HANDLE m_inputPipeRead;
		HANDLE m_inputPipeWrite;
		HANDLE m_outputPipeRead;
		HANDLE m_outputPipeWrite;

		//结束后残留的输出
		char* m_lastOutputBuffer;
		DWORD m_lastOutputBufferLen;


		//监视线程
		static void CheckProcThread(ConsoleProgram_Sync* const classthis) {
			while (1) {
				//进入锁
				classthis->m_rwProcMutex.lock_shared();

				if (classthis->m_processHandle == NULL) {
					//没有创建进程

					//判断是否在析构
					if (classthis->m_isExit) {
						//正在析构，解锁后退出
						classthis->m_rwProcMutex.unlock_shared();
						return;
					}

					//等待，先释放锁
					classthis->m_rwProcMutex.unlock_shared();
					//睡眠并且进入下一次循环
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					continue;
				}

				//释放锁，然后等待进程结束
				classthis->m_rwProcMutex.unlock_shared();
				WaitForSingleObject(classthis->m_processHandle, INFINITE);

				//进程结束了

				//重新进入锁
				classthis->m_rwProcMutex.lock();

				//设置状态为假
				classthis->m_processStatus = false;

				//为了防止无剩余输出导致其它线程仍在等待，这里解锁可能正在等待的线程
				classthis->UnlockOutput();

				//获取进程退出代码
				DWORD exitCode = 0;
				GetExitCodeProcess(classthis->m_processHandle, &exitCode);

				//设置进程退出代码
				classthis->m_processExitCode = exitCode;

				//安全关闭句柄
				Clhandle_s(classthis->m_inputPipeRead);
				Clhandle_s(classthis->m_inputPipeWrite);
				Clhandle_s(classthis->m_outputPipeRead);
				Clhandle_s(classthis->m_outputPipeWrite);
				Clhandle_s(classthis->m_processHandle);

				//判断是否在析构
				if (classthis->m_isExit) {
					//释放锁后退出
					classthis->m_rwProcMutex.unlock();
					return;
				}

				//进程已结束，获取剩余输出
				classthis->GetLastOutput();

				//资源回收工作完成，解锁，进入下一循环
				classthis->m_rwProcMutex.unlock();
			}
		}

		//解锁输出管道(无锁)
		void UnlockOutput() {
			CancelIoEx(m_outputPipeRead, NULL);
		}

		//获取最后的输出(无锁)
		void GetLastOutput() {
			DWORD bytesRead;
			PeekNamedPipe(m_outputPipeRead, NULL, 0, NULL, &bytesRead, NULL);
			if (bytesRead > 0) {
				if (m_lastOutputBuffer != NULL) {
					delete[] m_lastOutputBuffer;
				}
				m_lastOutputBuffer = new char[bytesRead + 1];
				m_lastOutputBufferLen = bytesRead;
				ReadFile(m_outputPipeRead, m_lastOutputBuffer, bytesRead, &bytesRead, NULL);
				m_lastOutputBuffer[bytesRead] = 0;
			}
		}


	public:

		/*
		构造时传入：文件路径 [工作目录]
		默认工作目录为文件所在目录
		*/
		ConsoleProgram_Sync(const std::string& programPath, const std::string& workingDirectory = "", const std::string& commandLineArgument = "")
			: m_programPath(programPath), m_workingDirectory(workingDirectory), m_commandLineArgument(commandLineArgument)
			, m_processExitCode(STILL_ACTIVE), m_inputPipeRead(NULL), m_inputPipeWrite(NULL), m_outputPipeRead(NULL), m_outputPipeWrite(NULL), m_processHandle(NULL) {
			//初始化指针
			m_lastOutputBuffer = NULL;
			m_lastOutputBufferLen = 0;
			//处理工作目录
			if (workingDirectory.empty()) {
				size_t found = programPath.find_last_of("/\\");
				if (found != std::string::npos) {
					this->m_workingDirectory = programPath.substr(0, found);
				}
			}
			//启动监视线程
			m_isExit = false;
			m_processStatus = false;
			m_thread = std::thread(&CheckProcThread, this);
		}

		~ConsoleProgram_Sync() {
			//设置正在析构标志
			m_rwProcMutex.lock();
			m_isExit = true;
			m_rwProcMutex.unlock();

			//停止控制台程序运行
			Stop();

			//等待线程结束运行
			m_thread.join();

			//释放指针内容
			m_rwProcMutex.lock();
			if (m_lastOutputBuffer != NULL) {
				delete[] m_lastOutputBuffer;
			}
			m_rwProcMutex.unlock();

			//析构工作完成
		}

		//启动进程
		bool Start() {
			//判断进程是否启动
			if (getProcessStatus()) {
				return false;
			}

			//获取锁
			m_rwProcMutex.lock();

			//再次判断是否启动，防止等待时发生更改
			if (m_processStatus) {
				m_rwProcMutex.unlock();
				return false;
			}

			//初始化安全标识符，使得管道可被子进程访问
			SECURITY_ATTRIBUTES securityAttributes;
			securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
			securityAttributes.bInheritHandle = TRUE;
			securityAttributes.lpSecurityDescriptor = NULL;

			//附带安全标识符创建输入管道
			if (!CreatePipe(&m_inputPipeRead, &m_inputPipeWrite, &securityAttributes, 0)) {
				//安全关闭句柄
				Clhandle_s(m_inputPipeRead);
				Clhandle_s(m_inputPipeWrite);

				//解锁
				m_rwProcMutex.unlock();
				return false;
			}

			//附带安全标识符创建输出管道
			if (!CreatePipe(&m_outputPipeRead, &m_outputPipeWrite, &securityAttributes, 0)) {
				//安全关闭句柄
				Clhandle_s(m_inputPipeRead);
				Clhandle_s(m_inputPipeWrite);
				Clhandle_s(m_outputPipeRead);
				Clhandle_s(m_outputPipeWrite);

				//解锁
				m_rwProcMutex.unlock();
				return false;
			}

			//初始化启动信息结构体
			STARTUPINFOA startupInfo;
			ZeroMemory(&startupInfo, sizeof(startupInfo));
			startupInfo.cb = sizeof(startupInfo);

			//设置输入输出管道，设置使用自定义管道标志位
			startupInfo.hStdInput = m_inputPipeRead;
			startupInfo.hStdOutput = m_outputPipeWrite;
			startupInfo.hStdError = m_outputPipeWrite;
			startupInfo.dwFlags |= STARTF_USESTDHANDLES;

			//初始化进程信息结构体
			PROCESS_INFORMATION processInfo;
			ZeroMemory(&processInfo, sizeof(processInfo));

			//处理命令行信息
			std::string commandLine = "\"" + m_programPath + "\"";
			if (!m_commandLineArgument.empty()) {
				commandLine += " " + m_commandLineArgument;
			}
			char* commandLine_c = new char[commandLine.size() + 1];
			strncpy(commandLine_c, commandLine.c_str(), commandLine.size());

			//创建进程
			if (!CreateProcessA(NULL, commandLine_c, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, m_workingDirectory.c_str(), &startupInfo, &processInfo)) {
				//释放命令行文本
				delete[] commandLine_c;

				//安全关闭句柄
				Clhandle_s(m_inputPipeRead);
				Clhandle_s(m_inputPipeWrite);
				Clhandle_s(m_outputPipeRead);
				Clhandle_s(m_outputPipeWrite);

				//解锁
				m_rwProcMutex.unlock();
				return false;
			}

			//释放命令行文本
			delete[] commandLine_c;

			//关闭线程句柄
			CloseHandle(processInfo.hThread);

			//保存进程句柄
			m_processHandle = processInfo.hProcess;

			//设置进程状态
			m_processStatus = true;

			//解锁
			m_rwProcMutex.unlock();
			return true;
		}

		/*
		停止进程运行，支持两种方式
		1.传入需要输入的命令(需要包含换行符)，以及非0的超时时间。
		将输入命令并且等待进程自行结束，如果超时就强制结束进程
		注意：超时时间大于0小于20ms按20ms计
		2.不传参数或传入其他情况的参数则强制结束进程
		返回是否超时，如果未指定超时时间或超时时间为0，则始终返回false
		*/
		bool Stop(const std::string& input = "", int timeoutMilliseconds = 0) {
			//获取锁
			m_rwProcMutex.lock_shared();

			//判断参数情况
			if ( (!input.empty()) && (timeoutMilliseconds != 0) ) {
				//先尝试输入命令，等待进程自然结束

				//判断状态，进程已经结束就没有必要再结束了
				if (!m_processStatus) {
					m_rwProcMutex.unlock_shared();
					return false;
				}

				//输入命令
				DWORD bytesWritten;
				WriteFile(m_inputPipeWrite, input.c_str(), input.size(), &bytesWritten, NULL);

				//释放锁
				m_rwProcMutex.unlock_shared();

				//计时等待进程结束
				bool isTimeout = false;
				do {
					//睡眠20ms
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					timeoutMilliseconds -= 20;
					if (timeoutMilliseconds <= 0) {
						isTimeout = true;
						break;
					}
				} while (getProcessStatus());

				//判断是自然结束还是超时了
				if (isTimeout) {
					//超时了就强制结束进程

					//获取锁
					m_rwProcMutex.lock_shared();

					//判断进程是否结束
					if (m_processStatus) {
						//强制结束进程
						TerminateProcess(m_processHandle, 0);

						//结束进程后释放锁
						m_rwProcMutex.unlock_shared();

						//等待进程结束
						int cnt = 0;
						do {
							//睡眠20ms
							std::this_thread::sleep_for(std::chrono::milliseconds(20));
							++cnt;
							if (cnt > 6000) {
								throw 1;
							}
						} while (getProcessStatus());
					} else {
						//直接解锁后返回
						m_rwProcMutex.unlock_shared();
					}
					return true;
				}
			} else {
				//判断状态，进程已经结束就没有必要再结束了
				if (!m_processStatus) {
					m_rwProcMutex.unlock_shared();
					return false;
				}

				//强制结束进程
				TerminateProcess(m_processHandle, 0);
				if (m_isExit) {
					m_rwProcMutex.unlock_shared();
					return false;
				}
				//释放锁
				m_rwProcMutex.unlock_shared();

				//等待进程结束
				int cnt = 0;
				do {
					//睡眠20ms
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					++cnt;

					if (cnt > 6000) {
						throw 1;
					}
				} while (getProcessStatus());
			}
			return false;
		}

		/*
		输入函数
		接收C风格字符串
		*/
		void Input(const char* input, DWORD len) {
			//获取锁
			m_rwProcMutex.lock_shared();

			//判断进程是否启动，未启动就直接返回
			if (!m_processStatus) {
				m_rwProcMutex.unlock_shared();
				return;
			}

			//把文本写入输入管道
			DWORD bytesWritten;
			WriteFile(m_inputPipeWrite, input, len, &bytesWritten, NULL);

			//解锁
			m_rwProcMutex.unlock_shared();
		}

		/*
		  输入函数
		  接收string类，原样输入，所以string需要内含换行符
		*/
		void Input(const std::string& input) {
			//获取锁
			m_rwProcMutex.lock_shared();

			//判断进程是否启动，未启动就直接返回
			if (!m_processStatus) {
				m_rwProcMutex.unlock_shared();
				return;
			}

			//把文本写入输入管道
			DWORD bytesWritten;
			WriteFile(m_inputPipeWrite, input.c_str(), input.size(), &bytesWritten, NULL);

			//解锁
			m_rwProcMutex.unlock_shared();
		}

		/*
		  输入一行
		  接收string类，会添加换行符，可指定换行符风格
		  换行符风格未指定时，默认采用Windows的CRLF风格
		*/
		void InputLine(const std::string& input, NewlineStyle newlineStyle = NewlineStyle::CRLF) {
			//获取锁
			m_rwProcMutex.lock_shared();

			//判断进程是否启动，未启动就直接返回
			if (!m_processStatus) {
				m_rwProcMutex.unlock_shared();
				return;
			}

			//把文本写入输入管道
			DWORD bytesWritten;
			WriteFile(m_inputPipeWrite, input.c_str(), input.size(), &bytesWritten, NULL);

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
			WriteFile(m_inputPipeWrite, newline, strlen(newline), &bytesWritten, NULL);

			//解锁
			m_rwProcMutex.unlock_shared();
		}

		/*
		拉取输出
		同步方式读取输出，如果没有输出就会一直等待，直到获取到输出才返回
		返回实际写入的字节数目。如果在等待过程中进程自然结束，那么返回0
		保证在数据的末尾有\0，这个\0不计入"实际写入的字节数目"
		*/
		DWORD PullOutput(char* buffer, DWORD bufferSize) {
			//获取读锁
			m_rwProcMutex.lock_shared();

			//判断进程是否启动
			if (!m_processStatus) {
				//释放读锁
				m_rwProcMutex.unlock_shared();

				//获取写锁
				m_rwProcMutex.lock();

				//再次判断状态，防止等待时状态更改
				if (m_processStatus) {
					m_rwProcMutex.unlock();
					return PullOutput(buffer, bufferSize);
				}

				if (m_lastOutputBuffer != NULL) {
					//说明有剩下的数据没有取出，这里模拟从管道取出数据
					if (m_lastOutputBufferLen < bufferSize) {
						//一次性搞定，直接复制进去即可
						strncpy(buffer, m_lastOutputBuffer, m_lastOutputBufferLen);
						buffer[m_lastOutputBufferLen] = 0;

						//删除字符串的内存空间
						delete[] m_lastOutputBuffer;
						m_lastOutputBuffer = NULL;

						//解锁
						m_rwProcMutex.unlock();

						return m_lastOutputBufferLen;
					} else {
						//需要分次拷贝

						//复制缓冲区能承受的最大长度
						strncpy(buffer, m_lastOutputBuffer, bufferSize - 1);
						buffer[bufferSize - 1] = 0;

						//新开辟空间存剩下的部分
						m_lastOutputBufferLen = m_lastOutputBufferLen - bufferSize + 1;
						char* tmp = new char [m_lastOutputBufferLen + 1];

						//复制到新空间，并且释放旧的
						strncpy(tmp, m_lastOutputBuffer + bufferSize - 1, m_lastOutputBufferLen);
						tmp[m_lastOutputBufferLen] = 0;
						delete[] m_lastOutputBuffer;
						m_lastOutputBuffer = tmp;

						//解锁
						m_rwProcMutex.unlock();

						return bufferSize - 1;
					}
				}
				//解锁
				m_rwProcMutex.unlock();
				buffer[0] = '\0';
				return 0;
			}

			HANDLE hpipe = m_outputPipeRead;

			//释放锁
			m_rwProcMutex.unlock_shared();

			//读取内容，进入读取专用锁
			m_outputMutex.lock();

			//判断管道是否变化
			if (hpipe != m_outputPipeRead) {
				//管道变了，解锁退出
				m_outputMutex.unlock();
				buffer[0] = '\0';
				return 0;
			}

			//读入数据
			DWORD bytesRead;
			ReadFile(hpipe, buffer, bufferSize - 1, &bytesRead, NULL);

			//退出读取专用锁
			m_outputMutex.unlock();

			buffer[bytesRead] = '\0';
			return bytesRead;
		}

		//返回进程状态，正在运行返回true，否则返回false
		bool getProcessStatus() {
			m_rwProcMutex.lock_shared();
			if (m_processStatus) {
				m_rwProcMutex.unlock_shared();
				return true;
			} else {
				m_rwProcMutex.unlock_shared();
				return false;
			}
		}

		//返回进程退出代码，未启动时为STILL_ACTIVE，启动后和GetExitCodeProcess结果一致
		DWORD getProcessExitCode() {
			m_rwProcMutex.lock_shared();
			DWORD exitCode = m_processExitCode;
			m_rwProcMutex.unlock_shared();
			return exitCode;
		}
};
