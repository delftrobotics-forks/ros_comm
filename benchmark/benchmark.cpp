#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "../include/xmlrpcpp/XmlRpcValue.h"

constexpr int count = 1000;

std::chrono::microseconds timeEncode(std::string & raw) {
	auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < count; ++i) {
		std::string xml = XmlRpc::XmlRpcValue(&raw[0], raw.size()).toXml();
	}
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
}
std::chrono::microseconds timeDecode(std::string & raw) {
	std::string xml = XmlRpc::XmlRpcValue(&raw[0], raw.size()).toXml();
	auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < count; ++i) {
		int offset = 0;
		XmlRpc::XmlRpcValue value;
		value.fromXml(xml, &offset);
	}
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
}

int main() {
	while (std::cin.good()) {
		std::string line;
		std::getline(std::cin, line);
		if (!std::cin) break;
		std::cout << line.size() << ", " << timeEncode(line).count() << ", " << timeDecode(line).count() << "\n";
	}
}
