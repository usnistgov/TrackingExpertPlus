#pragma once
/*

*/

//std
#include <vector>

#include "FileUtils.h"

//gl_support
#include "ModelOBJ.h"

class PartDatabase
{
private:
	std::vector<cs557::OBJModel*> models;

public:
	PartDatabase();
	~PartDatabase();

	/*
	Load objects from .obj files into PartDatabase given a .txt file that
	lists, line by line, the paths and filenames of each .obj file to be loaded
	(e.g. D:/ModelsPath/OBJModels/model_to_be_loaded.obj).  The models previously
	stored in this PartDatabase are cleared when calling this function.

	@param path - the path of the .txt file to load .obj files from.
	*/
	void loadObjsFromFile(std::string path);

	/*
	Get the object specified by the given line of the .txt file used to
	load .obj files from.

	@param id - the id of the object to be returned, given by its line number in the .txt file.
	
	@return a pointer to the OBJModel stored at the given id
	*/
	cs557::OBJModel* getObj(int id);
};