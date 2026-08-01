#include "bsp_format.h"

#include <iostream>

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "usage: bsp_smoke_test <map.bsp>\n";
		return 2;
	}

	q3::bsp::BspData data;
	std::string error;
	if (!q3::bsp::load_file(argv[1], data, error)) {
		std::cerr << error << "\n";
		return 1;
	}

	std::cout << "entities=" << data.entities.size() << "\n";
	std::cout << "shaders=" << data.shaders.size() << "\n";
	std::cout << "draw_vertices=" << data.draw_vertices.size() << "\n";
	std::cout << "draw_indexes=" << data.draw_indexes.size() << "\n";
	std::cout << "surfaces=" << data.surfaces.size() << "\n";
	std::cout << "models=" << data.models.size() << "\n";
	return 0;
}
