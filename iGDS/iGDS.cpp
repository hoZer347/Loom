#include "iGDS.h"

#include <Eigen/Core>
#include <open3d/Open3D.h>


namespace iGDS
{
	void Init()
	{
		auto visualizer = std::make_shared<open3d::visualization::Visualizer>();
		visualizer->CreateVisualizerWindow("Tooth Isolation");

		auto& renderOptions = visualizer->GetRenderOption();
		renderOptions.light_on_ = false;

		auto sphere0 = open3d::geometry::TriangleMesh::CreateSphere(0.5);
		auto sphere1= open3d::geometry::TriangleMesh::CreateSphere(0.5);
		auto sphere2 = open3d::geometry::TriangleMesh::CreateSphere(0.5);

		sphere0->Translate(Eigen::Vector3d( 1, 0, 0));
		sphere1->Translate(Eigen::Vector3d( 0, 0, 0));
		sphere2->Translate(Eigen::Vector3d(-1, 0, 0));

		visualizer->AddGeometry(sphere0);
		visualizer->AddGeometry(sphere1);
		visualizer->AddGeometry(sphere2);

		visualizer->Run();
	};
};
