#include <stdio.h>
#include <tclap/CmdLine.h>

int main(int argc, char** argv) {
	try {
		TCLAP::CmdLine cmd("Device driver for FLIR Lepton", ' ', "0.1A");
		TCLAP::UnlabeledValueArg<std::string> uart_file_arg("uart",
																												"UART file to open",
																												true, "","uart");
		TCLAP::UnlabeledValueArg<std::string> video_file_arg("video",
																												"Video file to open",
																												true, "","video");
		
		cmd.add(uart_file_arg);
		cmd.add(video_file_arg);
		cmd.parse(argc, argv);
		
		std::string uart_file_name = uart_file_arg.getValue();
		std::string video_file_name = video_file_arg.getValue();
		std::cout << uart_file_name << video_file_name << std::endl;
	} catch (TCLAP::ArgException &e) {
		printf("error: %s for arg %s\n", e.error().c_str(), e.argId().c_str());
	}
}
