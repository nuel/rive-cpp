#ifndef _RIVE_POLYGON_HPP_
#define _RIVE_POLYGON_HPP_
#include "generated/shapes/polygon_base.hpp"
#include "shapes/path_vertex.hpp"
#include "shapes/straight_vertex.hpp"
#include <vector>
namespace rive
{
	class Polygon : public PolygonBase
	{
	protected:
		std::vector<StraightVertex> m_PolygonVertices;

	public:
		Polygon();
		~Polygon();
		void update(ComponentDirt value) override;

	protected:
		void cornerRadiusChanged() override;
		void pointsChanged() override;
		virtual std::size_t vertexCount();
		virtual void buildPolygon();
	};
} // namespace rive

#endif