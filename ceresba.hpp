

#ifndef CERES_BA
#define CERES_BA

#include "sfm.h"

//#ifdef CERES_LIB
//ceres lib
#include "ceres/ceres.h"
#include "ceres/rotation.h"
//#endif

#include "export.hpp"
#include "defines.hpp"
#include "imagedata.hpp"
#include "rotation.hpp"
#include "distortion.hpp"
//
#include "CalcAngle.h"
#include "ba.hpp"



//corelib matrix
#include "Matrix.h"

#include <vector>
using namespace std;

#define MIN_MATCH_NUM 256

/////////////////////////////////////////////////////////////////
//for ceres
// Templated pinhole camera model for used with Ceres.  The camera is
// parameterized using 9 parameters: 3 for rotation, 3 for translation, 1 for
// focal length and 2 for radial distortion. The principal point is not modeled
// (i.e. it is assumed be located at the image center).

//#ifdef CERES_LIB


struct SFMReprojectionErrorEularAngle 
{
	SFMReprojectionErrorEularAngle(double observed_x, double observed_y)
		: observed_x(observed_x), observed_y(observed_y) {}

	template <typename T>
	bool operator()(const T* const cameraIn,  //3 interior camera parameters
		const T* const cameraOut, //6 outer camera parameters
		const T* const point,     //3 parameters for ground point
		T* residuals) const       //2 output residual parameters
	{
		T focal = cameraIn[0];
		T k1    = cameraIn[1];
		T k2    = cameraIn[2];

		T ax, ay, az;
		ax = cameraOut[0];
		ay = cameraOut[1];
		az = cameraOut[2];
		T R[9];
		GenerateRMatrixDirect(ax, ay, az, R);

		T t[3];
		t[0] = cameraOut[3];
		t[1] = cameraOut[4];
		t[2] = cameraOut[5];
				
		T ix1,iy1;
		GrdToImgWithDistort(point[0], point[1], point[2], &ix1, &iy1, R, t, focal, T(0), T(0), k1, k2);

		residuals[0] = ix1 - T(observed_x);
		residuals[1] = iy1 - T(observed_y);

		return true;
	}

	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x,
		const double observed_y) {
			return (new ceres::AutoDiffCostFunction<SFMReprojectionErrorEularAngle, 2, 3, 6, 3>(
				new SFMReprojectionErrorEularAngle(observed_x, observed_y)));
	}

	double observed_x;
	double observed_y;
};


// model: RX + T
struct SFMReprojectionError 
{
	SFMReprojectionError(double observed_x, double observed_y)
		: observed_x(observed_x), observed_y(observed_y) {}
	
	template <typename T>
	bool operator()(const T* const cameraIn,  //3 interior camera parameters
					const T* const cameraOut, //6 outer camera parameters
					const T* const point,     //3 parameters for ground point
					T* residuals) const       //2 output residual parameters
	{
		T focal = cameraIn[0];
		T k1    = cameraIn[1];
		T k2    = cameraIn[2];

		T aa[3];
		aa[0] = cameraOut[0];
		aa[1] = cameraOut[1];
		aa[2] = cameraOut[2];

		T t[3];
		t[0] = cameraOut[3];
		t[1] = cameraOut[4];
		t[2] = cameraOut[5];

		T p[3];
		ceres::AngleAxisRotatePoint(aa, point, p);

		/*
		T Rt[9];
		aa2rot(aa, Rt);		
		T R[9];
		for(int j=0; j<3; j++)
		{
			for(int i=0; i<3; i++)
			{
				R[j*3+i] = Rt[i*3 + j];
			}
		}
		*/
		
		p[0] += t[0];
		p[1] += t[1];
		p[2] += t[2];

		T sx = p[0] / p[2] * (-focal);
		T sy = p[1] / p[2] * (-focal);

		T dx = sx;
		T dy = sy;
		Undistort(sx, sy, dx, dy, k1, k2);
		
		residuals[0] = dx - T(observed_x);
		residuals[1] = dy - T(observed_y);
		
		//printf("residual: %lf %lf \n", residuals[0], residuals[1]);

		return true;
	}
	
	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x,
		const double observed_y) {
			return (new ceres::AutoDiffCostFunction<SFMReprojectionError, 2, 3, 6, 3>(
				new SFMReprojectionError(observed_x, observed_y)));
	}

	double observed_x;
	double observed_y;
};



// model: RX + T, 3 unknown position parameters
struct SFMPanoErrorFixedAngle
{
	SFMPanoErrorFixedAngle(double observed_x, double observed_y, double ax, double ay, double az, double radius)
	: observed_x(observed_x), observed_y(observed_y), radius(radius), ax(ax),ay(ay),az(az) {}

	template <typename T>
	bool operator()(const T* const cameraOut, //6 exterior camera parameters
		const T* const point,     //3 parameters for ground point
		T* residuals) const       //2 output residual parameters
	{
		T aa[3];
		aa[0] = T(ax);
		aa[1] = T(ay);
		aa[2] = T(az);

		T t[3];
		t[0] = cameraOut[0];
		t[1] = cameraOut[1];
		t[2] = cameraOut[2];

		T p[3];
		ceres::AngleAxisRotatePoint(aa, point, p);
		p[0] += t[0];
		p[1] += t[1];
		p[2] += t[2];


		T dx, dy;
		GrdToPanoImageCenter(p[0], p[1], p[2], T(radius), dx, dy);
		//residuals[0] = sqrt((dx - T(observed_x))*(dx - T(observed_x)) + (dy - T(observed_y))*(dy - T(observed_y)));
		residuals[0] = dx - T(observed_x);
		residuals[1] = dy - T(observed_y);


		/*
		T ox,oy,oz;
		PanoImageCenterToGrd(T(observed_x), T(observed_y), T(radius), ox, oy, oz);
		T n1 = sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2])+0.000001;
		T n2 = sqrt(ox*ox + oy*oy + oz*oz) + 0.000001;
		T dotv = p[0]*ox + p[1]*oy + p[2]*oz;
		T angle = acos( dotv / (n1*n2) );
		residuals[0] = angle*T(radius);
		*/

		//printf("residual: %lf %lf \n", residuals[0], residuals[1]);

		return true;
	}

	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x, const double observed_y, double ax, double ay, double az, const double radius)
	{
		return (new ceres::AutoDiffCostFunction<SFMPanoErrorFixedAngle, 2, 3, 3>(
			new SFMPanoErrorFixedAngle(observed_x, observed_y, ax, ay, az, radius)));
	}

	double observed_x;
	double observed_y;
	double ax, ay, az; //three angles
	double radius;
};



// model: RX + T, 6 unknown parameters
struct SFMPanoCtrlPtReprojectionError
{
	SFMPanoCtrlPtReprojectionError(double observed_x, double observed_y, double radius, double gx, double gy, double gz)
	: observed_x(observed_x), observed_y(observed_y), radius(radius), gx(gx), gy(gy), gz(gz) {}

	template <typename T>
	bool operator()(const T* const cameraOut, //6 exterior camera parameters
		//const T* const point,     //3 parameters for ground point
		T* residuals) const       //2 output residual parameters
	{
		T aa[3];
		aa[0] = cameraOut[0];
		aa[1] = cameraOut[1];
		aa[2] = cameraOut[2];

		T t[3];
		t[0] = cameraOut[3];
		t[1] = cameraOut[4];
		t[2] = cameraOut[5];

		T point[3]; //control point
		point[0] = T(gx);
		point[1] = T(gy);
		point[2] = T(gz);

		T p[3];
		ceres::AngleAxisRotatePoint(aa, point, p);
		p[0] += t[0];
		p[1] += t[1];
		p[2] += t[2];


		T dx, dy;
		GrdToPanoImageCenter(p[0], p[1], p[2], T(radius), dx, dy);
		//residuals[0] = sqrt((dx - T(observed_x))*(dx - T(observed_x)) + (dy - T(observed_y))*(dy - T(observed_y)));
		residuals[0] = (dx - T(observed_x))*32.0;
		residuals[1] = (dy - T(observed_y))*32.0;


		/*
		T ox,oy,oz;
		PanoImageCenterToGrd(T(observed_x), T(observed_y), T(radius), ox, oy, oz);
		T n1 = sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2])+0.000001;
		T n2 = sqrt(ox*ox + oy*oy + oz*oz) + 0.000001;
		T dotv = p[0]*ox + p[1]*oy + p[2]*oz;
		T angle = acos( dotv / (n1*n2) );
		residuals[0] = angle*T(radius);
		*/

		//printf("residual: %lf %lf \n", residuals[0], residuals[1]);

		return true;
	}

	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x, const double observed_y, const double radius, 
		const double gx, const double gy, const double gz)
	{
		return (new ceres::AutoDiffCostFunction<SFMPanoCtrlPtReprojectionError, 2, 6>(
			new SFMPanoCtrlPtReprojectionError(observed_x, observed_y, radius, gx, gy, gz)));
	}

	double observed_x;  //image x
	double observed_y;  //image y
	double radius;
	double gx, gy, gz;  //control points
};


// model: RX + T, 6 unknown parameters
struct SFMPanoReprojectionError 
{
	SFMPanoReprojectionError(double observed_x, double observed_y, double radius)
		: observed_x(observed_x), observed_y(observed_y), radius(radius) {}
	
	template <typename T>
	bool operator()( const T* const cameraOut, //6 exterior camera parameters
					 const T* const point,     //3 parameters for ground point
					 T* residuals) const       //2 output residual parameters
	{
		T aa[3];
		aa[0] = cameraOut[0];
		aa[1] = cameraOut[1];
		aa[2] = cameraOut[2];

		T t[3];
		t[0] = cameraOut[3];
		t[1] = cameraOut[4];
		t[2] = cameraOut[5];

		T p[3];
		ceres::AngleAxisRotatePoint(aa, point, p);
		p[0] += t[0];
		p[1] += t[1];
		p[2] += t[2];
			
		
		T dx,dy;
		GrdToPanoImageCenter(p[0], p[1], p[2], T(radius), dx, dy);
		//residuals[0] = sqrt((dx - T(observed_x))*(dx - T(observed_x)) + (dy - T(observed_y))*(dy - T(observed_y)));
		residuals[0] = dx - T(observed_x);
		residuals[1] = dy - T(observed_y);
		

		/*
		T ox,oy,oz;
		PanoImageCenterToGrd(T(observed_x), T(observed_y), T(radius), ox, oy, oz);
		T n1 = sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2])+0.000001;
		T n2 = sqrt(ox*ox + oy*oy + oz*oz) + 0.000001;
		T dotv = p[0]*ox + p[1]*oy + p[2]*oz;
		T angle = acos( dotv / (n1*n2) );
		residuals[0] = angle*T(radius);
		*/

		//printf("residual: %lf %lf \n", residuals[0], residuals[1]);

		return true;
	}
	
	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x, const double observed_y, const double radius) 
	{
			return (new ceres::AutoDiffCostFunction<SFMPanoReprojectionError, 2, 6, 3>(
				new SFMPanoReprojectionError(observed_x, observed_y, radius)));
	}

	double observed_x;
	double observed_y;
	double radius;
};


//when the POS data are right, only the ground points are optimaized, xdh, 2018.3.4
//when the coordinates of track points are fixed, only optimize the camera intrinsic parameters 
struct RefinePanoErrorFixedCamera
{
	RefinePanoErrorFixedCamera(double observed_x,double observed_y,double a0,double a1,double a2,double tx,double ty,double tz,double radius)
	:observed_x(observed_x),observed_y(observed_y),a0(a0),a1(a1),a2(a2),tx(tx),ty(ty),tz(tz),radius(radius) {}

	template <typename T>
	bool operator()(const T* const grd,   //3 ground point coordinates
		 T* residuals) const              //2 output residual parameters
	{
		T aa[3];
		aa[0] = T(a0);
		aa[1] = T(a1);
		aa[2] = T(a2);

		T t[3];
		t[0] = T(tx);
		t[1] = T(ty);
		t[2] = T(tz);

		T point[3];
		point[0] = grd[0];
		point[1] = grd[1];
		point[2] = grd[2];

		//RX + T
		T p[3];
		ceres::AngleAxisRotatePoint(aa, point, p);

		p[0] += t[0];
		p[1] += t[1];
		p[2] += t[2];


		T dx, dy;
		GrdToPanoImageCenter(p[0], p[1], p[2], T(radius), dx, dy);
		residuals[0] = dx - T(observed_x);
		residuals[1] = dy - T(observed_y);


		/*
		T ox,oy,oz;
		PanoImageCenterToGrd(T(observed_x), T(observed_y), T(radius), ox, oy, oz);
		T n1 = sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2])+0.000001;
		T n2 = sqrt(ox*ox + oy*oy + oz*oz) + 0.000001;
		T dotv = p[0]*ox + p[1]*oy + p[2]*oz;
		T angle = acos( dotv / (n1*n2) );
		residuals[0] = angle*T(radius);
		*/

		return true;
	}
	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x, const double observed_y,
		const double a0, const double a1, const double a2, 
		const double tx, const double ty, const double tz,
		const double radius) 
	{
		return (new ceres::AutoDiffCostFunction<RefinePanoErrorFixedCamera, 2, 3>(
			    new RefinePanoErrorFixedCamera(observed_x,observed_y,a0,a1,a2,tx,ty,tz,radius)));
	}

	double observed_x;
	double observed_y;
	double a0,a1,a2; //angle
	double tx,ty,tz; //position
	double radius;
};




//when the coordinates of track points are fixed, only optimize the camera intrinsic parameters 
struct RefinePanoCameraError 
{
	RefinePanoCameraError(double observed_x, double observed_y, double gx, double gy, double gz, double radius)
		: observed_x(observed_x), observed_y(observed_y), gx(gx), gy(gy), gz(gz), radius(radius) {}
	
	template <typename T>
	bool operator()(const T* const cameraOut, //6 outer camera parameters
					T* residuals) const       //2 output residual parameters
	{
		T aa[3];
		aa[0] = cameraOut[0];
		aa[1] = cameraOut[1];
		aa[2] = cameraOut[2];

		T t[3];
		t[0] = cameraOut[3];
		t[1] = cameraOut[4];
		t[2] = cameraOut[5];

		T point[3];
		point[0] = T(gx);
		point[1] = T(gy);
		point[2] = T(gz);

		//RX + T
		T p[3];
		ceres::AngleAxisRotatePoint(aa, point, p);

		p[0] += t[0];
		p[1] += t[1];
		p[2] += t[2];

		
		T dx,dy;
		GrdToPanoImageCenter(p[0], p[1], p[2], T(radius), dx, dy);
		residuals[0] = dx - T(observed_x);
		residuals[1] = dy - T(observed_y);
		

		/*
		T ox,oy,oz;
		PanoImageCenterToGrd(T(observed_x), T(observed_y), T(radius), ox, oy, oz);
		T n1 = sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2])+0.000001;
		T n2 = sqrt(ox*ox + oy*oy + oz*oz) + 0.000001;
		T dotv = p[0]*ox + p[1]*oy + p[2]*oz;
		T angle = acos( dotv / (n1*n2) );
		residuals[0] = angle*T(radius);
		*/

		return true;
	}
	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x,	const double observed_y, 
		const double gx, const double gy, const double gz, const double radius) {
			return (new ceres::AutoDiffCostFunction<RefinePanoCameraError, 2, 6>(
				new RefinePanoCameraError(observed_x, observed_y, gx, gy, gz, radius) ) );
	}

	double observed_x;
	double observed_y;
	double gx,gy,gz;
	double radius;
};


//when the coordinates of track points are fixed, only optimize the camera intrinsic parameters 
struct RefineCameraError 
{
	RefineCameraError(double observed_x, double observed_y, double gx, double gy, double gz)
		: observed_x(observed_x), observed_y(observed_y), gx(gx), gy(gy), gz(gz) {}
	
	template <typename T>
	bool operator()(const T* const cameraIn,  //3 interior camera parameters
					const T* const cameraOut, //6 outer camera parameters
					T* residuals) const       //2 output residual parameters
	{
		/*
		T focal = cameraIn[0];
		T k1    = cameraIn[1];
		T k2    = cameraIn[2];

		T aa[3];
		aa[0] = cameraOut[0];
		aa[1] = cameraOut[1];
		aa[2] = cameraOut[2];

		T t[3];
		t[0] = cameraOut[3];
		t[1] = cameraOut[4];
		t[2] = cameraOut[5];
				
		T Rt[9];
		aa2rot(aa, Rt);		
		T R[9];
		for(int j=0; j<3; j++)
		{
			for(int i=0; i<3; i++)
			{
				R[j*3+i] = Rt[i*3 + j];
			}
		}
		T ix1,iy1;
		GrdToImgWithDistortFixedPt(gx, gy, gz, &ix1, &iy1, R, t, focal, T(0), T(0), k1, k2);
		
		residuals[0] = ix1 - T(observed_x);
		residuals[1] = iy1 - T(observed_y);
		*/

		T focal = cameraIn[0];
		T k1    = cameraIn[1];
		T k2    = cameraIn[2];

		T aa[3];
		aa[0] = cameraOut[0];
		aa[1] = cameraOut[1];
		aa[2] = cameraOut[2];

		T t[3];
		t[0] = cameraOut[3];
		t[1] = cameraOut[4];
		t[2] = cameraOut[5];

		T point[3];
		point[0] = T(gx);
		point[1] = T(gy);
		point[2] = T(gz);

		//RX + T
		T p[3];
		ceres::AngleAxisRotatePoint(aa, point, p);

		p[0] += t[0];
		p[1] += t[1];
		p[2] += t[2];

		T sx = p[0] / p[2] * (-focal);
		T sy = p[1] / p[2] * (-focal);

		T dx = sx;
		T dy = sy;
		Undistort(sx, sy, dx, dy, k1, k2);
		
		residuals[0] = dx - T(observed_x);
		residuals[1] = dy - T(observed_y);
		
		return true;
	}
	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x,	const double observed_y, 
		const double gx, const double gy, const double gz) {
			return (new ceres::AutoDiffCostFunction<RefineCameraError, 2, 3, 6>(
				new RefineCameraError(observed_x, observed_y, gx, gy, gz) ) );
	}

	double observed_x;
	double observed_y;
	double gx,gy,gz;
};



//when the coordinates of track points and camera focal length are fixed, 
//only optimize the camera extrinsic parameters 
struct RefineCameraFixedFocalLen 
{
	RefineCameraFixedFocalLen(double observed_x, double observed_y, double focal_len, double gx, double gy, double gz)
		: observed_x(observed_x), observed_y(observed_y), focal_len(focal_len), gx(gx), gy(gy), gz(gz) {}
	
	template <typename T>
	bool operator()(const T* const cameraOut, //6 extrinsic camera parameters
					T* residuals) const       //2 output residual parameters
	{
		T focal = T(focal_len);

		T aa[3];
		aa[0] = cameraOut[0];
		aa[1] = cameraOut[1];
		aa[2] = cameraOut[2];

		T t[3];
		t[0] = cameraOut[3];
		t[1] = cameraOut[4];
		t[2] = cameraOut[5];

		T point[3];
		point[0] = T(gx);
		point[1] = T(gy);
		point[2] = T(gz);

		//RX + T
		T p[3];
		ceres::AngleAxisRotatePoint(aa, point, p);

		p[0] += t[0];
		p[1] += t[1];
		p[2] += t[2];

		T sx = p[0] / p[2] * (-focal);
		T sy = p[1] / p[2] * (-focal);

		T dx = sx;
		T dy = sy;
		//Undistort(sx, sy, dx, dy, k1, k2);
		
		residuals[0] = dx - T(observed_x);
		residuals[1] = dy - T(observed_y);
		
		return true;
	}
	// Factory to hide the construction of the CostFunction object from
	// the client code.
	static ceres::CostFunction* Create(const double observed_x,	const double observed_y, 
		const double focal_len, const double gx, const double gy, const double gz) {
			return (new ceres::AutoDiffCostFunction<RefineCameraFixedFocalLen, 2, 6>(
				new RefineCameraFixedFocalLen(observed_x, observed_y, focal_len, gx, gy, gz) ) );
	}

	double observed_x;
	double observed_y;
	double gx,gy,gz;
	double focal_len;
};


int RemoveBadPoints(vector<TrackInfo>& trackSeq, 
	vector<ImgFeature>& imageFeatures, vector<CameraPara>& cameras );

//bundle adjustment for multiple cameras
int CeresBA( vector<TrackInfo> trackSeq, vector<ImgFeature> imageFeatures, vector<CameraPara> &cameras);

//bundle adjustment for camera parameters only
int CeresBA( vector<Point3DDouble>& grdPts, vector<Point2DDouble>& imgPts, CameraPara& camera, 
			 bool bIsFixedFocalLen );

int SelectNewImage(vector<int> cameraVisited,
	vector<TrackInfo>& tracks, 
	vector<TrackInfo>& trackSeqNew );

int RemoveOutlierPts( vector<TrackInfo>& tracks, vector<TrackInfo>& trackSeq, 
	vector<ImgFeature>& imageFeatures, vector<int> cameraIDOrder, 
	vector<CameraPara>& cameras);

int RemoveOutlierPtsWithPos(vector<TrackInfo>& tracks, 
	vector<ImgFeature>& imageFeatures, vector<int> cameraIDOrder,
	vector<CameraPara>& cameras, double az);

int RunBA( vector<TrackInfo>& trackSeq, vector<ImgFeature>& imageFeatures, 
	vector<int> cameraIDOrder, vector<CameraPara> &cameras);

int CalculateNewCamParas(int nCameraId, 
	vector<ImgFeature>&  imageFeatures,
	vector<TrackInfo>& trackSeqNew, 
	vector<TrackInfo>& tracks,
	double initFocalLen,
	CameraPara& cam );

int UpdateBATracks( int newCameraIndex, 
	vector<int>&  cameraVisited,
	vector<int>&  cameraIDOrder,
	vector<ImgFeature>&  imageFeatures, 
	vector<TrackInfo>& tracks, 
	vector<TrackInfo>& trackSeqNew,
	vector<CameraPara>& cameras);

//refine the camera parameters and track points 
int RefineAllParameters(vector<TrackInfo>& trackSeq, vector<ImgFeature>& imageFeatures, 
	vector<int> cameraIDOrder, vector<TrackInfo>& tracks, vector<CameraPara>& cameras);


int RefineAllParametersWithPos(vector<TrackInfo>& tracks, vector<ImgFeature>& imageFeatures,
	vector<int> cameraIDOrder, vector<CameraPara>& cameras, double az);


//calculate the ground point of track seq
DLL_EXPORT int CaculateTrackSeqGrd(vector<ImgFeature>&  imageFeatures,
	vector<TrackInfo>&   tracks,
	vector<CameraPara>&  cameras,
	bool explicit_camera_centers);


DLL_EXPORT int CalculateCtrlPt(vector<stCtrlPt>& ctrlpts, 
	vector<CameraPara>&  cameras,
	bool explicit_camera_centers);


//find the tracks with the specified camera index
//vector<TrackInfo> FindLocalTracks(int camid, vector<TrackInfo>& curTracks);
vector<TrackInfo> FindLocalTracks(int camid, vector<TrackInfo>& curTracks,
	vector<TrackInfo>& allTracks, vector<ImgFeature>& imageFeatures);

vector<int> GetCameraInTracks(vector<TrackInfo>& tracks);


class DLL_EXPORT CCeresBA: public CBABase
{
public:
	CCeresBA();
	~CCeresBA();

	int RunSFM( vector<Point3DDouble> pt3, vector<ImageKeyVector> ptViews, 
		vector<ImgFeature> imageFeatures,  vector<int> cameraIDOrder,
		vector<CameraPara>& cameras);

	//
	int BundleAdjust( int numCameras, vector<CameraPara>& cameras,vector<ImgFeature> imageFeatures, 
		vector<PairMatchRes> pairMatchs, vector<TrackInfo> tracks, char* outDir);


	//new interface, including more input parameters
	int BundleAdjust(int numCameras, vector<CameraPara>& cameras, vector<CImageDataBase*> imageData, 
		vector<PairMatchRes> pairMatchs, vector<TrackInfo> tracks, char* outDir);


	int BundleAdjust(int numCameras, vector<CameraPara>& cameras, vector<ImgFeature>& imageFeatures, 
		vector<PairMatchRes>& pairMatchs, vector<TrackInfo>& tracks, bool& bIsStop);

	void SetProgress(double* pdProgress);

    void SetCallBack(sfmCall cf) { mpCallFunc = cf; }

private:
	//double *mpdProgress;
    double mdProgress;
    sfmCall mpCallFunc;
};

#endif


//#endif
