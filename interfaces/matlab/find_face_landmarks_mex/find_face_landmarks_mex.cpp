#include "MxArray.hpp"

// std
#include <vector>
#include <string>
#include <exception>

// sfl
#include <sfl/sequence_face_landmarks.h>

// vsal
#include <vsal/VideoStreamFactory.h>
#include <vsal/VideoStreamOpenCV.h>

// OpenCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

// Matlab
#include <mex.h>

// Namespaces
using std::vector;
using std::string;
using std::runtime_error;

#define printfFnc(...) { mexPrintf(__VA_ARGS__); mexEvalString("drawnow;");}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	try
	{
		// Parse arguments
		std::string inputPath, landmarksModelPath;
		int device = -1;
		int width = 0, height = 0;
		float frame_scale = 1.0f;
		bool preview = true;
		if (nrhs == 0) throw runtime_error("No parameters specified!");
		if (nrhs < 2) throw runtime_error("Invalid number of parameters!");

		if (!MxArray(prhs[0]).isChar()) throw runtime_error(
			"modelFile must be a string containing the path to the model file!");
		landmarksModelPath = MxArray(prhs[0]).toString();

		if (MxArray(prhs[1]).isChar())			// Dataset
		{
			inputPath = MxArray(prhs[1]).toString();
			if (nrhs > 2) frame_scale = (float)MxArray(prhs[2]).toDouble();
			if (nrhs > 3) preview = MxArray(prhs[3]).toBool();
		}
		else if (MxArray(prhs[1]).isInt32() || MxArray(prhs[1]).isDouble())	// Live video
		{
			device = MxArray(prhs[1]).toInt();
			if (nrhs > 2) width = MxArray(prhs[2]).toInt();
			if (nrhs > 3) height = MxArray(prhs[3]).toInt();
			if (nrhs > 4) frame_scale = (float)MxArray(prhs[4]).toDouble();
		}
		else throw runtime_error("Second parameter must be either a sequence path or a device id!");

		// Initialize Sequence Face Landmarks
		std::shared_ptr<sfl::SequenceFaceLandmarks> sfl =
			sfl::SequenceFaceLandmarks::create(landmarksModelPath, frame_scale);

		// Create video source
		vsal::VideoStreamFactory& vsf = vsal::VideoStreamFactory::getInstance();
		std::unique_ptr<vsal::VideoStreamOpenCV> vs(
			(vsal::VideoStreamOpenCV*)vsf.create(inputPath));
		if (vs == nullptr) throw runtime_error("No video source specified!");

		// Open video source
		if (!vs->open()) throw runtime_error("Failed to open video source!");

		// Main loop
		cv::Mat frame;
		int faceCounter = 0;
		while (vs->read())
		{
			if (!vs->isUpdated()) continue;

			frame = vs->getFrame();
			const sfl::Frame& landmarks_frame = sfl->addFrame(frame);

			if (preview)
			{
				faceCounter += landmarks_frame.faces.size();

				// Render landmarks
				sfl::render(frame, landmarks_frame);

				// Render overlay
				string msg = "Faces found so far: " + std::to_string(faceCounter);
				cv::putText(frame, msg, cv::Point(15, 15),
					cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1, CV_AA);
				cv::putText(frame, "press any key to stop", cv::Point(10, frame.rows - 20),
					cv::FONT_HERSHEY_COMPLEX, 0.5, cv::Scalar(255, 255, 255), 1, CV_AA);

				// Show frame
				cv::imshow("find_face_landmarks", frame);
				int key = cv::waitKey(1);
				if (key >= 0) break;
			}
		}

		///
		// Output results
		///
		const std::vector<sfl::Frame>& frames = sfl->getSequence();

		// Create the frames as a 1-by-n array of structs.
		mwSize dims[2] = { 1, 1 };
		dims[1] = frames.size();
		const char *frame_fields[] = { "faces", "width", "height" };
		const char *face_fields[] = { "landmarks", "bbox" };
		plhs[0] = mxCreateStructArray(2, dims, 3, frame_fields);

		// For each frame
		for (size_t i = 0; i < frames.size(); ++i)
		{
			const sfl::Frame& frame = frames[i];

			// Set the width and height to the fields of the current frame
			mxSetField(plhs[0], i, frame_fields[1], MxArray(frame.width));
			mxSetField(plhs[0], i, frame_fields[2], MxArray(frame.height));

			if (frame.faces.empty()) continue;

			// Create the faces as a 1-by-n array of structs.
			dims[1] = frame.faces.size();
			mxArray* facesStructArray = mxCreateStructArray(2, dims, 2, face_fields);

			// Set the faces to the field of the current frame
			mxSetField(plhs[0], i, frame_fields[0], facesStructArray);

			// For each face
			for (size_t j = 0; j < frame.faces.size(); ++j)
			{
				const sfl::Face& face = frame.faces[j];

				// Convert the landmarks to Matlab's pixel format
				cv::Mat_<int> landmarks(face.landmarks.size(), 2, (int*)face.landmarks.data());
				landmarks += 1;

				// Set the landmarks to the field of the current face
				mxSetField(facesStructArray, j, face_fields[0], MxArray(landmarks));

				// Convert the bounding box to Matlab's pixel format
				cv::Mat bbox = (cv::Mat_<int>(1, 4) <<
					face.bbox.x + 1, face.bbox.y + 1, face.bbox.width, face.bbox.height);

				// Set the bounding to the field of the current face
				mxSetField(facesStructArray, j, face_fields[1], MxArray(face.bbox));
			}
		}

		// Cleanup
		cv::destroyWindow("find_face_landmarks");
	}
	catch (std::exception& e)
	{
		mexErrMsgIdAndTxt("dlib_find_face_landmarks:parsing", "Error: %s", e.what());
	}
}