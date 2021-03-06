#include <openvdb/openvdb.h>
#include <openvdb/tools/Interpolation.h>
#include <openvdb/tools/ValueTransformer.h>

#include <fstream>
#include <iostream>
#include <algorithm>

void printUsage() {
    std::cout << "Usage: volconv filename.vdb [gridname (optional)]" << std::endl;
}

template<typename T>
void write(std::ofstream &f, T data) {
    f.write(reinterpret_cast<const char *>(&data), sizeof(data));
}

int write_grid(openvdb::GridBase::Ptr base_grid, std::string output_filename, std::string outputformat, bool normalise_values) {
	openvdb::FloatGrid::Ptr grid = openvdb::gridPtrCast<openvdb::FloatGrid>(base_grid);

	auto bbox_dim = grid->evalActiveVoxelDim();
	auto bbox = grid->evalActiveVoxelBoundingBox();

	auto ws_min = grid->indexToWorld(bbox.min());
	auto ws_max = grid->indexToWorld(bbox.max() - openvdb::Vec3R(1, 1, 1));
	openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::BoxSampler> sampler(*grid);
	// Compute the value of the grid at fractional coordinates in index space.

	std::cout << "\tGrid Dims: [" << bbox.max().x() << ", " << bbox.max().y() << ", " << bbox.max().z() << "]" << std::endl;
	std::cout << "\tGrid bbox positions from [" << ws_min.x() << ", " << ws_min.y() << ", " << ws_min.z() << "] to [" << ws_max.x() << ", " << ws_max.y() << ", " << ws_max.z() << "]" << std::endl;
	
	std::vector<float> values;
	for (int k = bbox.min().z(); k < bbox.max().z(); ++k) {
		for (int j = bbox.min().y(); j < bbox.max().y(); ++j) {
			for (int i = bbox.min().x(); i < bbox.max().x(); ++i) {                
				float value = sampler.isSample(openvdb::Vec3R(i, j, k));
				values.push_back(value);
			}
		}
	}
	if (outputformat == "ascii") {
		std::ofstream output_file(output_filename);
		if (!output_file.is_open()) {
			std::cout << "\tCould not open output file!\n";
			return -1;
		}
		for (size_t i = 0; i < values.size(); ++i) {
			if (i > 0) {
				output_file << ", ";
			}
			output_file << std::to_string(values[i]);
		}
		output_file.close();
	} else if (outputformat == "binary") {
		std::ofstream output_file(output_filename, std::ios::binary);
		if (!output_file.is_open()) {
			std::cout << "\tCould not open output file!\n";
			return -1;
		}
		output_file << 'V';
		output_file << 'O';
		output_file << 'L';
		uint8_t version = 3;
		write(output_file, version);

		write(output_file, (int32_t) 1); // type
		write(output_file, (int32_t)bbox_dim.x() - 1);
		write(output_file, (int32_t)bbox_dim.y() - 1);
		write(output_file, (int32_t)bbox_dim.z() - 1);
		write(output_file, (int32_t) 1); // #n channels

		float xmin = ws_min.x();
		float ymin = ws_min.y();
		float zmin = ws_min.z();
		float xmax = ws_max.x();
		float ymax = ws_max.y();
		float zmax = ws_max.z();
		write(output_file, xmin);
		write(output_file, ymin);
		write(output_file, zmin);
		write(output_file, xmax);
		write(output_file, ymax);
		write(output_file, zmax);
		
		auto values_max = *std::max_element(values.begin(), values.end());
		auto values_min = *std::min_element(values.begin(), values.end());
		
		if (normalise_values) {
			std::cout << "\tNormalising grid with min=" << values_min << " max=" << values_max << std::endl;
		}

		for (size_t i = 0; i < values.size(); ++i) {
			if (normalise_values) {
				values[i] = (values[i] - values_min) / (values_max - values_min);
			}
			write(output_file, values[i]);
		}
		
		output_file.close();
		return 0;
	} else {
		std::cerr << "Invalid output format: " << outputformat << std::endl;
		printUsage();
		return -1;
	}
}

int main(int argc, char **argv) {
    if (argc == 1) {
        std::cerr << "Error: Too few arguments" << std::endl;
        printUsage();
        return -1;
    }
	
	for (auto i = 0; i < argc; ++i) {
		std::cout << "[" << i << "] " << argv[i] << std::endl;
	}
	std::cout << std::endl;

    std::string filename = argv[1];
    std::string gridname = "";
	bool normalise_values = false;
    if (argc >= 3) {
        gridname = argv[2];
    }
	
	if (argc >= 4) {
		normalise_values = true;
	}
	
	std::cout << normalise_values << std::endl;

    std::string outputformat = "binary";
    if (argc == 5) {
        outputformat = argv[4];
    }

    openvdb::initialize();
    openvdb::io::File file(filename);
    file.open();
	
	if (gridname == "ALL") {
		openvdb::GridBase::Ptr base_grid;
		for (openvdb::io::File::NameIterator name_iter = file.beginName(); name_iter != file.endName(); ++name_iter) {
			base_grid = file.readGrid(name_iter.gridName());
			std::string output_filename = filename.substr(0, filename.find_last_of('.')) + "_" + name_iter.gridName() + ".vol";
			std::cout << "Writing grid [" << name_iter.gridName() << "] to " << output_filename << std::endl;
			auto ret = write_grid(base_grid, output_filename, outputformat, normalise_values);
			if (ret != 0) {
				return ret;
			}
		}
	}
	else {
		// Loop over all grids in the file and retrieve a shared pointer
		// to the one named "LevelSetSphere".  (This can also be done
		// more simply by calling file.readGrid("LevelSetSphere").)
		openvdb::GridBase::Ptr base_grid;
		std::string output_filename;
		for (openvdb::io::File::NameIterator name_iter = file.beginName();
			name_iter != file.endName(); ++name_iter)
		{
			// Read in only the grid we are interested in.
			if (gridname == "" || name_iter.gridName() == gridname) {
				base_grid = file.readGrid(name_iter.gridName());
				output_filename = filename.substr(0, filename.find_last_of('.')) + "_" + name_iter.gridName() + ".vol";
				std::cout << "Writing grid [" << name_iter.gridName() << "] to " << filename << std::endl;
				if (gridname == "")
					break;
			} else {
				std::cout << "skipping grid " << name_iter.gridName() << std::endl;
			}
		}
		auto ret = write_grid(base_grid, output_filename, outputformat, normalise_values);
		if (ret != 0) {
			return ret;
		}
	}
    file.close();
}
