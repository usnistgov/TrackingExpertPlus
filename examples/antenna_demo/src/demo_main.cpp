/*
 Gear transmission box demo.

 This code implements a test to verify the camera and point cloud producer 
 functionality. Currently, a Structure Core camera is used to acquire 
 depth data. The PointCloudProducer instance processes the depth data
 and generates a 3D point cloud. 

 The entire PointCloudProducer instance works with Nvidia Cuda. Thus,
 the code tests whether or not sufficient Cuda capabilities are given. 

 Rafael Radkowski
 Iowa State University
 Oct 2019
 rafael@iastate.edu
 +1 (515) 294 7044

-----------------------------------------------------------------




*/
// STL
#include <iostream>
#include <string>
#include <fstream>

// TrackingExpert
#include "trackingx.h"
#include "graphicsx.h"
#include "ICaptureDevice.h"

#include "KinectAzureCaptureDevice.h"  // the camera
#include "PointCloudProducer.h"
#define _WITH_PRODUCER

// local
#include "AntennaRenderer.h"
#include "PartDatabase.h"
#include "ProcedureRenderer.h"

// instance of a structure core camera 
KinectAzureCaptureDevice* camera;

// The OpenGL window
isu_ar::GLViewer* window;


// demo content 
PartDatabase* database;
AntennaRenderer* renderer;
ProcedureRenderer* proc_renderer;

// The Video Background
isu_gfx::GLVideoCanvas*	video_bg;
cv::Mat img_color;
cv::Mat img_ref;

/*
The main render and processing loop. 
@param pm - projection matrix
@param vm - camera view matrix. 
*/
void render_loop(glm::mat4 pm, glm::mat4 vm) {

	// fetch a new frame	
	camera->getRGBFrame(img_ref);
	memcpy(img_color.ptr(), img_ref.ptr(), img_ref.rows * img_ref.cols * sizeof(CV_8UC4));

	video_bg->draw(pm, vm, glm::mat4());

	//renderer->draw(pm, vm);
	proc_renderer->draw(pm, vm);

}

void getKey(int key, int action)
{
	switch (action)
	{
	case 0: //Key up
		break;

	case 1: //Key down
		switch (key)
		{
		case 68: //d
			//renderer->progress(true);
			proc_renderer->progress(true);
			break;
		case 65: //a
			//renderer->progress(false);
			proc_renderer->progress(false);
			break;
		}
		break;
	}
}


int main(int argc, char* argv)
{
	std::cout << "TrackingExpert+ Antenna Demo" << endl;
	std::cout << "Version 0.9" << endl;
	std::cout << "-----------------------------" << endl;

	/*
	Open a camera device. 
	*/
	camera =  new KinectAzureCaptureDevice(0, KinectAzureCaptureDevice::Mode::RGBIRD, false);

	/*
	Test if the camera is ready to run. 
	*/
	if (!camera->isOpen()) {
		std::cout << "\n[ERROR] - Cannot access camera." << std::endl;
		return -1;
	}

	/*
	create the renderer.
	The renderer executes the main loop in this demo. 
	*/
	window = new isu_ar::GLViewer();
	window->create(camera->getCols(texpert::CaptureDeviceComponent::COLOR), camera->getRows(texpert::CaptureDeviceComponent::COLOR), "Antenna Demo");
	window->addRenderFcn(render_loop);
	window->addKeyboardCallback(getKey);
	window->setViewMatrix(glm::lookAt(glm::vec3(1.0f, 0.0, -0.5f), glm::vec3(0.0f, 0.0f, 0.f), glm::vec3(0.0f, 1.0f, 0.0f)));
	window->setClearColor(glm::vec4(1, 1, 1, 1));
	window->enableCameraControl(true);


	
	/*
	Create the video background
	*/
	camera->getRGBFrame(img_color);

	video_bg = new isu_gfx::GLVideoCanvas();
	video_bg->create(img_color.rows, img_color.cols, img_color.ptr(), true);

	/*
	Create the procedure
	*/
	std::vector<std::string> steps = std::vector<std::string>(5);
	steps.at(0) = "Step0";
	steps.at(1) = "Step1";
	steps.at(2) = "Step2";
	steps.at(3) = "Step3";
	steps.at(4) = "Step4";
	
	proc_renderer = new ProcedureRenderer();
	proc_renderer->init("ExAntennaProc.json", steps);

	///*
	//Load part models
	//*/
	//database = new PartDatabase();
	//database->loadObjsFromFile("../models/load_models.txt");

	///*
	//Load models into the GearBoxRenderer sequence
	//*/
	//renderer = new AntennaRenderer();
	//int idx = 0;
	//for (int i = 0; i < database->getNumModels(); i++)
	//{
	//	Model* curModel = database->getObj(i);
	//	if (!curModel->name.compare("null") == 0)
	//	{
	//		renderer->addModel(curModel, curModel->name);
	//		idx++;
	//	}
	//}

	//renderer->setSteps();
	//
	//renderer->updateInPlace();

	std::cout << "-----------------------------" << endl;
	std::cout << "Use the W and D keys to cycle through the stages of the assembly process." << endl;
	
	window->start();

	// cleanup
	delete camera;
	delete video_bg;
	delete database;
	delete window;

	return 1;
}