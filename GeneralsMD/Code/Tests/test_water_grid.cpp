/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
 * T1c, defect 17: the water grid steps once per logic frame, however many logic frames a pass runs.
 *
 * WaterGridMotion::updateForLogicFrame is the step moved out of WaterRenderObjClass::update; GameLogic::
 * update calls it (through TerrainVisual::updateWaterGrid) at the top of every logic frame.  This plays
 * Tests/water_grid_scenario.h through it that way, under EA's loop and three shapes of the fork's
 * catch-up, and requires every one to print what the ORIGINAL code printed under EA's loop.
 *
 * The golden is Tests/water_grid_oracle.cpp's first line: the original text, taken from the tree before T1 moved it, by
 * Tools/water_grid_oracle_extract.py and stepped on the client pass as W3DTerrainVisual::update did,
 * one pass per logic frame.  Identical from mingw-w64 under Wine, native arm64 and x86_64 under Rosetta.
 * The armed control is the oracle's other lines: the original under the catch-up schedules below, which
 * all differ from it (and whose grid is still moving at frame 240, where EA's has settled by 183).
 *
 * What it cannot see: that GameLogic::update makes the call, and makes it at the top of the frame - that
 * is the engine's loop, which needs the W3D device to run (read in the source, T1); the
 * client passes' own reads of the grid, which now see it a step earlier in a pass, and draw nothing
 * the simulation reads.
 */

#include "test_harness.h"

#include "PreRTS.h"
#include "Common/GameCommon.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameLogic/WaterGridMotion.h"

#include <string>
#include <vector>

#include "water_grid_scenario.h"

namespace {

// EA's loop, run by the original code: water_grid_oracle's "original passes of 1" line.
const char *const EA_LOOP_GOLDEN = "frames 240 all 96559c89 last a7eea444 moving 183";

void boot()
{
	static bool booted = false;
	if (booted)
		return;
	booted = true;
	initMemoryManager();
	TheNameKeyGenerator = NEW NameKeyGenerator;
	TheNameKeyGenerator->init();
	TheWritableGlobalData = NEW GlobalData;
	TheWritableGlobalData->m_gravity = ConvertAccelerationInSecsToFrames( -64.0f );	// GameData.ini's
}

// The engine as it is now: nothing on the client pass, the step at the top of each logic frame.
struct MovedEngine
{
	typedef WaterGridMotion::MeshPoint Point;
	std::vector<Point> points;
	Bool moving;
	int callsPerFrame;
	MovedEngine( int calls = 1 ) : points( water_grid::POINTS_X * water_grid::POINTS_Y ), moving( FALSE ), callsPerFrame( calls ) {}
	Point *mesh( void ) { return &points[0]; }
	Bool &inMotion( void ) { return moving; }
	void clientPass( UnsignedInt ) {}
	void logicFrameTop( UnsignedInt frame )
	{
		for (int i = 0; i < callsPerFrame; ++i)
			WaterGridMotion::updateForLogicFrame( frame, TRUE, moving, &points[0], water_grid::CELLS_X, water_grid::CELLS_Y );
	}
};

unsigned nextFirstFrame()
{
	static unsigned run = 0;
	return 1 + 1000 * run++;	// the step's last-frame gate is a static: no run starts where one ended
}

}  // namespace

TEST(water_grid_scenario_uses_the_engines_gravity)
{
	boot();
	const Real engine = ConvertAccelerationInSecsToFrames( -64.0f );
	const float scenario = water_grid::shippedGravity();
	CHECK( memcmp( &engine, &scenario, sizeof( engine ) ) == 0 );	// the oracle used the scenario's
}

TEST(water_grid_steps_as_eas_loop_did_under_eas_loop)
{
	boot();
	MovedEngine engine;
	const std::string line = water_grid::run( engine, std::vector<int>( 1, 1 ), nextFirstFrame() );
	printf( "  moved, passes of 1: %s\n", line.c_str() );
	CHECK_STR( line.c_str(), EA_LOOP_GOLDEN );
}

TEST(water_grid_k_logic_frames_in_one_pass_equal_k_single_passes)
{
	boot();
	const std::vector<std::vector<int> > schedules = water_grid::schedules();
	for (size_t s = 0; s < schedules.size(); ++s)
	{
		MovedEngine engine;
		const std::string line = water_grid::run( engine, schedules[s], nextFirstFrame() );
		printf( "  moved, passes of %s: %s\n", water_grid::describe( schedules[s] ).c_str(), line.c_str() );
		CHECK_STR( line.c_str(), EA_LOOP_GOLDEN );
	}
}

TEST(water_grid_a_second_call_for_the_same_frame_does_not_step_again)
{
	/* A frozen or held logic frame calls GameLogic::update again without advancing the frame; the
		 gate, EA's own, keeps it to one step, as one client pass per unchanged frame did. */
	boot();
	MovedEngine engine( 3 );
	const std::string line = water_grid::run( engine, std::vector<int>( 1, 2 ), nextFirstFrame() );
	CHECK_STR( line.c_str(), EA_LOOP_GOLDEN );
}

TEST(water_grid_unstepped_is_not_the_golden)
{
	// So the golden means something: with no step at all the result is different...
	boot();
	struct Unstepped : MovedEngine { void logicFrameTop( UnsignedInt ) {} } still;
	const std::string line = water_grid::run( still, std::vector<int>( 1, 1 ), nextFirstFrame() );
	CHECK( line != EA_LOOP_GOLDEN );
	// ...and the golden's grid was in motion on 183 of the 240 frames: pushed, then settled.
}

#include "Platform/WaterSurfaceModel.h"
#include "Platform/WaterProjectedMesh.h"
#include "Platform/WaterFftGeometry.h"
#include "Platform/WaterVesselMotion.h"
#include "Platform/WaterObstacleIndex.h"
#include "Platform/WaterOpticalContext.h"
#include <cmath>

namespace {
WaterOpticalContext::Polygon opticalRectangle(float x0,float y0,float x1,float y1,float level=4) {
	return {{{x0,y0,level},{x1,y0,level},{x1,y1,level},{x0,y1,level}}};
}
}

TEST(water_optical_context_keeps_enclosed_pools_clear_inside_an_ocean_polygon)
{
	using namespace WaterOpticalContext;
	const int width=50,height=40,border=2;
	std::vector<uint8_t> heights(width*height,20);
	for (int y=0;y<height;++y) for (int x=0;x<18;++x) heights[y*width+x]=0;
	for (int y=10;y<31;++y) for (int x=28;x<47;++x) heights[y*width+x]=0;
	std::vector<Polygon> polygons={opticalRectangle(-100,-100,600,500)};
	Field field;
	CHECK(Build(heights.data(),width,height,border,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[20*width+4],255);
	CHECK_EQ(field.ocean[20*width+37],0); // Broad, wet pool still disconnected from the coast.
	CHECK_EQ(field.ocean[20*width+22],0);
	CHECK_EQ(field.ocean[20*width+15],0); // Within 30 world units of shore.
	CHECK(field.ocean[20*width+12]>0 && field.ocean[20*width+12]<255);
	for (int x=1;x<18;++x) CHECK(field.ocean[20*width+x]<=field.ocean[20*width+x-1]);
}

TEST(water_optical_context_polygon_union_cannot_invent_a_water_connection)
{
	using namespace WaterOpticalContext;
	const int side=60;
	std::vector<uint8_t> heights(side*side,0);
	Field field;
	std::vector<Polygon> polygons={opticalRectangle(-100,-100,180,700),opticalRectangle(300,100,550,500)};
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[30*side+3],255);
	CHECK_EQ(field.ocean[30*side+42],0); // Low terrain between polygons is not authored water.
	CHECK_EQ(field.ocean[30*side+24],0);
	polygons.push_back(opticalRectangle(100,100,400,500,9));
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[30*side+42],0); // A different level cannot supply the missing connection.
	for (Point& p:polygons.back().points) p.z=4;
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[30*side+42],255); // Same-level overlapping polygons join correctly.
}

TEST(water_optical_context_uses_the_playable_boundary_and_has_no_texture_edge_shore)
{
	using namespace WaterOpticalContext;
	const int side=40,border=5;
	std::vector<uint8_t> heights(side*side,0);
	Field field;
	std::vector<Polygon> polygons={opticalRectangle(-100,-100,500,500)};
	CHECK(Build(heights.data(),side,side,border,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[0],255);
	CHECK_EQ(field.ocean[side*side-1],255);
	polygons={opticalRectangle(-50,-50,340,340)}; // Exact native grid bounds, both orientations.
	CHECK(Build(heights.data(),side,side,border,10,1,4,polygons,field));
	for (uint8_t value:field.ocean) CHECK_EQ(value,255);
	std::reverse(polygons[0].points.begin(),polygons[0].points.end());
	CHECK(Build(heights.data(),side,side,border,10,1,4,polygons,field));
	for (uint8_t value:field.ocean) CHECK_EQ(value,255);
	// Restrict water to the padded border: that alone does not mark it as ocean.
	polygons={opticalRectangle(-100,-100,-10,500)};
	CHECK(Build(heights.data(),side,side,border,10,1,4,polygons,field));
	for (uint8_t value:field.ocean) CHECK_EQ(value,0);
}

TEST(water_optical_context_concave_polygon_preserves_its_dry_notch)
{
	using namespace WaterOpticalContext;
	const int side=60;
	std::vector<uint8_t> heights(side*side,0);
	std::vector<Polygon> polygons={{{{-10,-10,4},{600,-10,4},{600,600,4},{400,600,4},
		{400,200,4},{200,200,4},{200,600,4},{-10,600,4}}}};
	Field field;
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[40*side+30],0);
	CHECK_EQ(field.ocean[10*side+30],255);
	CHECK_EQ(field.ocean[40*side+5],255);
	CHECK_EQ(field.ocean[40*side+55],255);
}

TEST(water_optical_context_refreshes_terrain_level_dimensions_and_polygon_geometry)
{
	using namespace WaterOpticalContext;
	const int side=40;
	std::vector<uint8_t> heights(side*side,20);
	for (int y=10;y<31;++y) for (int x=10;x<31;++x) heights[y*side+x]=0;
	std::vector<Polygon> polygons={opticalRectangle(-100,-100,500,500)};
	Field field;
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[20*side+20],0);
	for (int y=10;y<31;++y) for (int x=0;x<10;++x) heights[y*side+x]=0;
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[20*side+20],255);
	CHECK(Build(heights.data(),side,side,0,10,1,8,polygons,field));
	CHECK_EQ(field.ocean[20*side+20],0);
	const std::vector<Polygon> before=polygons;
	CHECK(SamePolygons(before,polygons));
	polygons[0].points[0].z=8;
	CHECK(!SamePolygons(before,polygons));
	polygons=before; polygons[0].points[0].x+=1;
	CHECK(!SamePolygons(before,polygons));
	CHECK(Build(heights.data(),20,20,1,10,1,4,before,field));
	CHECK_EQ(field.ocean.size(),400);
	CHECK_EQ(field.shore.size(),400);
}

TEST(water_optical_context_bounds_and_shore_fade)
{
	using namespace WaterOpticalContext;
	CHECK_EQ(Offshore(0),0);
	CHECK_EQ(Offshore(30),0);
	CHECK(std::abs(Offshore(65)-0.5f)<0.00001f);
	CHECK_EQ(Offshore(100),1);
	CHECK_EQ(Offshore(1000),1);
	std::vector<uint8_t> heights(16,0);
	std::vector<Polygon> polygons={opticalRectangle(-10,-10,100,100)};
	Field field;
	CHECK(Build(heights.data(),4,4,0,10,1,4,polygons,field));
	CHECK(!Build(heights.data(),MaxDimension+1,2,0,10,1,4,polygons,field));
	CHECK(field.ocean.empty() && field.shore.empty());
	CHECK(!Build(heights.data(),2048,2048,0,10,1,4,polygons,field));
	CHECK(!Build(heights.data(),4,4,2,10,1,4,polygons,field));
	CHECK(!Build(heights.data(),4,4,0,0,1,4,polygons,field));
	CHECK(!Build(NULL,4,4,0,10,1,4,polygons,field));
}

TEST(water_surface_analytic_normal_is_derivative_of_displaced_height)
{
	using namespace WaterSurfaceModel;
	for (int t=0;t<4;++t) for (int i=0;i<20;++i) {
		const float x=2.31f*i,y=-1.57f*i,time=0.73f*t,epsilon=0.002f;
		const Sample sample=Evaluate(x,y,time);
		const float dx=(Evaluate(x+epsilon,y,time).height-Evaluate(x-epsilon,y,time).height)/(2*epsilon);
		const float dy=(Evaluate(x,y+epsilon,time).height-Evaluate(x,y-epsilon,time).height)/(2*epsilon);
		CHECK(std::abs(sample.dx-dx)<0.003f);
		CHECK(std::abs(sample.dy-dy)<0.003f);
		CHECK(std::abs(sample.height)<MaxDisplacement);
	}
}

TEST(water_surface_modes_evolve_and_all_freeze_controls_hold_time)
{
	using namespace WaterSurfaceModel;
	// A single location can be at the same height at two different phases. Check the field's
	// motion energy instead, so destructive interference at one point is not a false failure.
	double heightChange=0, slopeChange=0;
	for (int i=0;i<16;++i) {
		const Sample a=Evaluate(12+i*3.7f,34-i*1.3f,0),b=Evaluate(12+i*3.7f,34-i*1.3f,1);
		heightChange+=(a.height-b.height)*(a.height-b.height);
		slopeChange+=(a.dx-b.dx)*(a.dx-b.dx)+(a.dy-b.dy)*(a.dy-b.dy);
	}
	CHECK(heightChange>0.1);
	CHECK(slopeChange>0.1);
	CHECK(AdvanceTime(2,0.1f,true,false,false)==2);
	CHECK(AdvanceTime(2,0.1f,false,true,false)==2);
	CHECK(AdvanceTime(2,0.1f,false,false,true)==2);
	CHECK(AdvanceTime(2,30,false,false,false)==2.1f);
	CHECK(AdvanceTime(2,-1,false,false,false)==2);
	const float resumed=AdvanceTime(2,0.02f,false,false,false);
	CHECK(resumed>2 && resumed<2.1f);
	std::string shader;
	AppendHLSL(shader);
	CHECK(shader.find("float3 waterWave")!=std::string::npos);
	CHECK(shader.find("value.yz")!=std::string::npos);
}

TEST(water_surface_shores_limit_render_displacement_without_changing_water_level)
{
	using namespace WaterSurfaceModel;
	CHECK(ShoreDamping(-10)==0);
	CHECK(ShoreDamping(0)==0);
	CHECK(ShoreDamping(9)==1);
	CHECK(ShoreDamping(100)==1);
	for (int depth=0;depth<90;++depth) CHECK(ShoreDamping(depth*0.1f)<=ShoreDamping((depth+1)*0.1f));
}

TEST(water_surface_shared_edges_weld_in_reverse_and_move_continuously_with_camera)
{
	using namespace WaterSurfaceModel;
	const Point a={-200,17,12},b={2100,533,12};
	const Point cameras[]={{500,200,110},{-100,-300,100},{3500,700,1000}};
	for (const Point &camera:cameras) for (int cells=1;cells<=MeshCells;cells*=2) for (int i=0;i<=cells;++i) {
		const float t=float(i)/cells;
		const Point forward=EdgePoint(a,b,t,camera),reverse=EdgePoint(b,a,1-t,camera);
		CHECK(memcmp(&forward,&reverse,sizeof(Point))==0);
		Point moved=camera;moved.x+=0.01f;
		const Point next=EdgePoint(a,b,t,moved);
		CHECK(std::abs(next.x-forward.x)<0.02f);
		CHECK(std::abs(next.y-forward.y)<0.02f);
	}
}

TEST(water_surface_mesh_stays_in_convex_patch_and_triangle_indices_are_bounded)
{
	using namespace WaterSurfaceModel;
	const Point corners[4]={{0,0,12},{600,10,12},{520,300,12},{-100,270,12}};
	const Point cameras[]={{0,0,1},{300,100,30},{-1000,200,100},{10000,-4000,1200}};
	for (const Point &camera:cameras) {
		std::vector<Point> mesh(MeshVertices);
		for (int j=0;j<=MeshCells;++j) for (int i=0;i<=MeshCells;++i) {
			const float u=float(i)/MeshCells,v=float(j)/MeshCells;
			const Point edges[4]={EdgePoint(corners[0],corners[1],u,camera),EdgePoint(corners[1],corners[2],v,camera),
				EdgePoint(corners[3],corners[2],u,camera),EdgePoint(corners[0],corners[3],v,camera)};
			const Point p=PatchPoint(corners,edges,u,v);
			mesh[j*(MeshCells+1)+i]=p;
			CHECK(std::abs(p.z-12)<0.0001f);
			for (int edge=0;edge<4;++edge) {
				const Point &a=corners[edge],&b=corners[(edge+1)%4];
				CHECK((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x)>-0.05f);
			}
		}
		for (int j=0;j<MeshCells;++j) for (int i=0;i<MeshCells;++i) {
			const int a=j*(MeshCells+1)+i,b=a+1,c=a+MeshCells+1,d=c+1;
			CHECK(a>=0 && b<MeshVertices && c<MeshVertices && d<MeshVertices);
			const Point &p=mesh[a],&q=mesh[b],&r=mesh[d],&s=mesh[c];
			CHECK((q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x)>0);
			CHECK((r.x-p.x)*(s.y-p.y)-(r.y-p.y)*(s.x-p.x)>0);
		}
	}
	for (int patches=1;patches<=2000;++patches) {
		const int cells=CellsForPatches(patches);
		CHECK(cells>=1 && cells<=MeshCells);
		CHECK(patches*(cells+1)*(cells+1)<=VertexBudget);
		int emitted=0;
		for (int row=0;row<cells;row+=IndexBatchRows(cells)) {
			const int rows=std::min(IndexBatchRows(cells),cells-row);
			CHECK(rows*cells*6<=65535);
			emitted+=rows*cells*6;
		}
		CHECK(emitted==cells*cells*6);
	}
}

TEST(water_surface_cannot_be_replaced_by_uniform_texture_advection)
{
	using namespace WaterSurfaceModel;
	// Fit the best possible single scrolling velocity to the temporal derivative. A static
	// translated height map has zero residual; dispersive independently directed modes do not.
	double xx=0,xy=0,yy=0,xt=0,yt=0;
	struct Derivative { double x,y,t; } values[64];
	for (int i=0;i<64;++i) {
		const float x=float(i%8)*7.13f,y=float(i/8)*9.77f,t=1.37f,epsilon=0.002f;
		const Sample sample=Evaluate(x,y,t);
		Derivative &v=values[i];v.x=sample.dx;v.y=sample.dy;
		v.t=(Evaluate(x,y,t+epsilon).height-Evaluate(x,y,t-epsilon).height)/(2*epsilon);
		xx+=v.x*v.x;xy+=v.x*v.y;yy+=v.y*v.y;xt+=v.x*v.t;yt+=v.y*v.t;
	}
	const double determinant=xx*yy-xy*xy;
	CHECK(determinant>0.001);
	const double vx=(xt*yy-yt*xy)/determinant,vy=(yt*xx-xt*xy)/determinant;
	double residual=0,energy=0;
	for (const Derivative &v:values) { const double e=v.t-vx*v.x-vy*v.y;residual+=e*e;energy+=v.t*v.t; }
	CHECK(residual/energy>0.2);
}

namespace {
struct LODTestVertex {
	WaterSurfaceModel::Point position;
	float attenuation,base;
};
LODTestVertex testLODMorph(const LODTestVertex &fine,const LODTestVertex &a,const LODTestVertex &b,float amount)
{
	using namespace WaterSurfaceModel;
	return {{MorphCoordinate(fine.position.x,a.position.x,b.position.x,amount),
		MorphCoordinate(fine.position.y,a.position.y,b.position.y,amount),
		MorphCoordinate(fine.position.z,a.position.z,b.position.z,amount)},
		MorphCoordinate(fine.attenuation,a.attenuation,b.attenuation,amount),MorphCoordinate(fine.base,a.base,b.base,amount)};
}
std::vector<LODTestVertex> testLODGrid(int cells,float amount,bool ocean=false)
{
	using namespace WaterSurfaceModel;
	const Point corners[4]={{0,0,12},{600,10,12},{520,300,12},{-100,270,12}},camera={180,75,200};
	std::vector<LODTestVertex> grid((cells+1)*(cells+1));
	for (int j=0;j<=cells;++j) for (int i=0;i<=cells;++i) {
		const float u=float(i)/cells,v=float(j)/cells;
		const Point edges[4]={EdgePoint(corners[0],corners[1],u,camera),EdgePoint(corners[1],corners[2],v,camera),
			EdgePoint(corners[3],corners[2],u,camera),EdgePoint(corners[0],corners[3],v,camera)};
		Point p=PatchPoint(corners,edges,u,v);
		const Sample blend={ocean?0.5f+0.0005f*p.x:0.0f,ocean?0.0005f:0.0f,0};
		const float attenuation=TerrainDamping(0.05f*(p.y+20),blend.height),base=p.z;
		p.z+=attenuation*EvaluateBlended(p.x,p.y,1.73f,blend).height;
		grid[j*(cells+1)+i]={p,attenuation,base};
	}
	if (amount>0) for (int j=0;j<=cells;++j) for (int i=0;i<=cells;++i) {
		if (((i|j)&1)==0) continue;
		const CoarsePair pair=CoarsePairForVertex(i,j,cells);
		grid[j*(cells+1)+i]=testLODMorph(grid[j*(cells+1)+i],grid[pair.first],grid[pair.second],amount);
	}
	return grid;
}
LODTestVertex testLODSample(const std::vector<LODTestVertex> &grid,int cells,float u,float v)
{
	const int i=std::min(int(u*cells),cells-1),j=std::min(int(v*cells),cells-1),side=cells+1;
	const float x=u*cells-i,y=v*cells-j;
	const LODTestVertex &a=grid[j*side+i],&d=grid[(j+1)*side+i+1];
	const LODTestVertex &other=y>x?grid[(j+1)*side+i]:grid[j*side+i+1];
	const float wa=1-std::max(x,y),wd=std::min(x,y),wo=std::abs(x-y);
	return {{a.position.x*wa+d.position.x*wd+other.position.x*wo,
		a.position.y*wa+d.position.y*wd+other.position.y*wo,
		a.position.z*wa+d.position.z*wd+other.position.z*wo},
		a.attenuation*wa+d.attenuation*wd+other.attenuation*wo,a.base*wa+d.base*wd+other.base*wo};
}
void checkLODVertexNear(const LODTestVertex &a,const LODTestVertex &b,float epsilon)
{
	CHECK(std::abs(a.position.x-b.position.x)<epsilon);
	CHECK(std::abs(a.position.y-b.position.y)<epsilon);
	CHECK(std::abs(a.position.z-b.position.z)<epsilon);
	CHECK(std::abs(a.attenuation-b.attenuation)<epsilon);
	CHECK(std::abs(a.base-b.base)<epsilon);
}
}

TEST(water_surface_lod_retains_freecam_detail_and_reduces_rts_cost_monotonically)
{
	using namespace WaterSurfaceModel;
	CHECK(LODForCameraHeight(128,117).cells==128);
	CHECK(LODForCameraHeight(128,120).morph==0);
	CHECK(LODForCameraHeight(128,180).cells==64);
	CHECK(LODForCameraHeight(128,270).cells==32);
	CHECK(LODForCameraHeight(128,405).cells==16);
	CHECK(LODForCameraHeight(128,607.5f).cells==8);
	CHECK(LODForCameraHeight(128,310).cells==32);
	CHECK(LODForCameraHeight(64,310).cells==16);
	for (int base=1;base<=MeshCells;base*=2) {
		int previous=base;
		float previousDetail=0;
		for (int height=-10;height<=3000;++height) {
			const MeshLOD lod=LODForCameraHeight(base,float(height));
			CHECK(lod.cells<=previous && lod.cells>=std::min(base,MinimumLODCells));
			CHECK(lod.morph>=0 && lod.morph<=1);
			const float detail=std::log2(float(base)/lod.cells)+lod.morph;
			CHECK(detail+0.00001f>=previousDetail);
			previous=lod.cells;previousDetail=detail;
		}
	}
}

TEST(water_surface_lod_coarse_pairs_stay_bounded_and_do_not_read_morphed_vertices)
{
	using namespace WaterSurfaceModel;
	for (int cells=1;cells<=MeshCells;cells*=2) for (int j=0;j<=cells;++j) for (int i=0;i<=cells;++i) {
		const CoarsePair pair=CoarsePairForVertex(i,j,cells);
		CHECK(pair.first>=0 && pair.second<(cells+1)*(cells+1));
		if (cells>1) {
			CHECK(((pair.first%(cells+1))&1)==0 && ((pair.first/(cells+1))&1)==0);
			CHECK(((pair.second%(cells+1))&1)==0 && ((pair.second/(cells+1))&1)==0);
		}
	}
}

TEST(water_surface_lod_completed_morph_equals_coarse_triangles_and_metadata)
{
	using namespace WaterSurfaceModel;
	const std::vector<LODTestVertex> fine=testLODGrid(32,1.0f),coarse=testLODGrid(16,0.0f);
	for (int j=0;j<32;++j) for (int i=0;i<32;++i) {
		// Sample both triangle interiors, not only their shared vertices: this catches a wrong
		// diagonal or an arbitrary nearest-vertex collapse that would still match even/even nodes.
		for (int triangle=0;triangle<2;++triangle) {
			const float u=(i+(triangle?0.23f:0.71f))/32.0f,v=(j+(triangle?0.73f:0.29f))/32.0f;
			checkLODVertexNear(testLODSample(fine,32,u,v),testLODSample(coarse,16,u,v),0.0002f);
		}
	}
	const std::vector<LODTestVertex> original=testLODGrid(32,0.0f);
	for (float amount:{0.1f,0.35f,0.7f}) {
		const std::vector<LODTestVertex> morphed=testLODGrid(32,amount);
		for (size_t i=0;i<morphed.size();++i) {
			CHECK(morphed[i].attenuation>=0 && morphed[i].attenuation<=1);
			CHECK(std::abs(morphed[i].position.z-morphed[i].base)<=MaxDisplacement);
			CHECK(morphed[i].position.x>=std::min(original[i].position.x,fine[i].position.x)-0.0001f);
			CHECK(morphed[i].position.x<=std::max(original[i].position.x,fine[i].position.x)+0.0001f);
		}
	}
}

TEST(water_surface_lod_transition_is_continuous_in_both_camera_directions)
{
	using namespace WaterSurfaceModel;
	for (float threshold:{180.0f,270.0f,405.0f,607.5f}) {
		const MeshLOD below=LODForCameraHeight(128,threshold-0.001f),above=LODForCameraHeight(128,threshold+0.001f);
		const std::vector<LODTestVertex> fine=testLODGrid(below.cells,below.morph),coarse=testLODGrid(above.cells,above.morph);
		for (int j=0;j<12;++j) for (int i=0;i<12;++i) {
			const float u=(i+0.17f)/12,v=(j+0.63f)/12;
			checkLODVertexNear(testLODSample(fine,below.cells,u,v),testLODSample(coarse,above.cells,u,v),0.0003f);
		}
	}
	CHECK(MorphColour(0x10203040,0xff00aa55,0x00ff55aa,0)==0x10203040);
	CHECK(MorphColour(0x10203040,0xff00aa55,0x00ff55aa,1)==0x80808080);
}

TEST(water_surface_lod_shared_edges_remain_bit_identical_throughout_morph)
{
	using namespace WaterSurfaceModel;
	const Point a={-200,17,12},b={2100,533,12},camera={500,200,110};
	for (int cells=8;cells<=MeshCells;cells*=2) {
		std::vector<LODTestVertex> forward(cells+1),reverse(cells+1);
		for (int i=0;i<=cells;++i) {
			Point p=EdgePoint(a,b,float(i)/cells,camera);
			const float attenuation=ShoreDamping((p.y-30)*0.07f);
			p.z+=Evaluate(p.x,p.y,1.73f).height*attenuation;
			forward[i]={p,attenuation,12};
			p=EdgePoint(b,a,float(i)/cells,camera);
			const float reverseAttenuation=ShoreDamping((p.y-30)*0.07f);
			p.z+=Evaluate(p.x,p.y,1.73f).height*reverseAttenuation;
			reverse[i]={p,reverseAttenuation,12};
		}
		for (float amount:{0.0f,0.17f,0.5f,0.93f,1.0f}) for (int i=0;i<=cells;++i) {
			const CoarsePair fp=CoarsePairForVertex(i,0,cells),rp=CoarsePairForVertex(cells-i,0,cells);
			const LODTestVertex f=testLODMorph(forward[i],forward[fp.first],forward[fp.second],amount);
			const LODTestVertex r=testLODMorph(reverse[cells-i],reverse[rp.first],reverse[rp.second],amount);
			CHECK(memcmp(&f,&r,sizeof(f))==0);
		}
	}
}


namespace {
struct IndexedTestObstacle {
	WaterObstacleIndex::Influence influence;
	float z,height,major,minor,cosine,sine;bool box,hasHull;
};
struct IndexedTestDamping { float surface,obstacle;size_t visited; };
IndexedTestDamping indexedTestDamping(const std::vector<IndexedTestObstacle> &objects,
	const WaterObstacleIndex::Grid *index,float x,float y,float z,float depth,float offshore)
{
	IndexedTestDamping result={WaterSurfaceModel::TerrainDamping(depth,offshore),1,0};
	if (result.surface<=0) return result;
	const WaterObstacleIndex::Range range=index && offshore>0?index->Query(x,y):WaterObstacleIndex::Range{};
	const size_t count=range.indexed?range.count:objects.size();
	for (size_t i=0;i<count;++i) {
		const IndexedTestObstacle &o=objects[range.indexed?range.indices[i]:i];++result.visited;
		if (z<o.z-12 || z>o.z+o.height+3) continue;
		const float dx=x-o.influence.x,dy=y-o.influence.y;
		if (offshore>0 && WaterSurfaceModel::BeyondObstacleFade(dx,dy,o.influence.radius)) continue;
		float damping;
		if (o.hasHull && offshore>0) {
			const float legacy=offshore<1?WaterSurfaceModel::ShoreDamping(std::sqrt(dx*dx+dy*dy)-o.influence.radius):1;
			damping=legacy+(1-legacy)*offshore;
		} else if (o.box && offshore>0) {
			damping=WaterSurfaceModel::BoxDamping(dx*o.cosine+dy*o.sine,-dx*o.sine+dy*o.cosine,o.major,o.minor);
			if (offshore<1) { const float legacy=WaterSurfaceModel::ShoreDamping(std::sqrt(dx*dx+dy*dy)-o.influence.radius);damping=legacy+(damping-legacy)*offshore; }
		} else damping=WaterSurfaceModel::ShoreDamping(std::sqrt(dx*dx+dy*dy)-o.influence.radius);
		result.obstacle=(std::min)(result.obstacle,damping);result.surface=(std::min)(result.surface,damping);
		if (result.surface<=0) return result;
	}
	return result;
}
}

TEST(water_obstacle_bins_match_ordered_damping_and_metadata_with_fewer_candidates)
{
	using namespace WaterObstacleIndex;
	std::vector<IndexedTestObstacle> obstacles;std::vector<Influence> influences;
	uint32_t random=0x5317a291u;
	auto unit=[&]() { random=random*1664525u+1013904223u;return float(random>>8)/16777216.0f; };
	for (int i=0;i<839;++i) {
		const float major=2+unit()*70,minor=2+unit()*30,angle=unit()*6.2831853f;
		IndexedTestObstacle o={{unit()*4096-2048,unit()*4096-2048,std::sqrt(major*major+minor*minor)},unit()*30,5+unit()*40,
			major,minor,std::cos(angle),std::sin(angle),i%3!=0,i%11==0};
		obstacles.push_back(o);influences.push_back(o.influence);
	}
	Grid index;CHECK(index.Build(influences,{-2200,-2200,2200,2200}));
	CHECK(index.CellCount()<=Grid::MaxCells && index.ReferenceCount()<=Grid::MaxReferences);
	size_t indexedVisits=0,bruteVisits=0;
	for (int i=0;i<4000;++i) {
		const float x=unit()*4400-2200,y=unit()*4400-2200,z=unit()*50,depth=unit()*14;
		const float offshore=i%5==0?0:(i%5==1?1:unit());
		const auto a=indexedTestDamping(obstacles,nullptr,x,y,z,depth,offshore),b=indexedTestDamping(obstacles,&index,x,y,z,depth,offshore);
		CHECK(a.surface==b.surface && a.obstacle==b.obstacle);
		if (offshore==0) CHECK(a.visited==b.visited);
		else { indexedVisits+=b.visited;bruteVisits+=a.visited; }
		const Range range=index.Query(x,y);
		for (size_t j=1;j<range.count;++j) CHECK(range.indices[j]>range.indices[j-1]);
	}
	CHECK(indexedVisits*20<bruteVisits); // Armed cost check: the implementation cannot return all839.
	// Movement/removal invalidates membership on the next draw; no old candidate list survives.
	for (int i=0;i<50;++i) { obstacles[i].influence.x+=317;obstacles[i].influence.y-=199; }
	obstacles.erase(obstacles.begin()+50,obstacles.begin()+80);influences.clear();
	for (const auto &o:obstacles) influences.push_back(o.influence);
	CHECK(index.Build(influences,{-2200,-2200,2200,2200}));
	for (int i=0;i<2000;++i) {
		const float x=unit()*5000-2500,y=unit()*5000-2500,offshore=unit();
		const auto a=indexedTestDamping(obstacles,nullptr,x,y,7,8,offshore),b=indexedTestDamping(obstacles,&index,x,y,7,8,offshore);
		CHECK(a.surface==b.surface && a.obstacle==b.obstacle);
	}
	index.Clear();CHECK(!index.Query(0,0).indexed);
}

TEST(water_obstacle_bins_include_negative_boundaries_and_fail_to_complete_full_list)
{
	using namespace WaterObstacleIndex;
	const std::vector<IndexedTestObstacle> obstacles={{{-64,-128,54},7,30,40,30,0.6f,0.8f,true,false},
		{{0,0,54},7,30,40,30,1,0,true,true},{{64,128,54},7,30,40,30,1,0,false,false}};
	std::vector<Influence> influences;for (const auto &o:obstacles) influences.push_back(o.influence);
	Grid index;CHECK(index.Build(influences,{-256,-256,256,256}));
	for (float x:{-128.0001f,-128.0f,-127.9999f,-64.0001f,-64.0f,-63.9999f,-0.0001f,0.0f,0.0001f,63.9999f,64.0f,64.0001f,127.9999f,128.0f,128.0001f})
		for (float y:{-192.0f,-128.0f,-64.0f,0.0f,64.0f,128.0f,192.0f}) for (float offshore:{0.0f,0.001f,0.5f,0.999f,1.0f}) {
			const auto a=indexedTestDamping(obstacles,nullptr,x,y,7,7,offshore),b=indexedTestDamping(obstacles,&index,x,y,7,7,offshore);
			CHECK(a.surface==b.surface && a.obstacle==b.obstacle);
		}
	for (Bounds bounds: {Bounds{-1e30f,-1e30f,1e30f,1e30f},Bounds{0,0,100000,100000},Bounds{NAN,0,1,1}}) {
		CHECK(!index.Build(influences,bounds));CHECK(!index.Query(0,0).indexed);
		const auto a=indexedTestDamping(obstacles,nullptr,0,0,7,7,0.5f),b=indexedTestDamping(obstacles,&index,0,0,7,7,0.5f);
		CHECK(a.surface==b.surface && a.obstacle==b.obstacle);
	}
	const std::vector<Influence> huge(20,Influence{0,0,100000});
	CHECK(!index.Build(huge,{-4000,-4000,4000,4000}));CHECK(!index.Query(0,0).indexed);
	CHECK(!index.Build({Influence{INFINITY,0,10}},{-128,-128,128,128}));
	CHECK(index.Build({}, {-128,-128,128,128}));CHECK(index.Query(0,0).indexed && index.Query(0,0).count==0);
	CHECK(!index.Query(INFINITY,0).indexed);
}

namespace {
WaterProjectedMesh::Projection testWaterProjection()
{
	WaterProjectedMesh::Projection p={{0.01,0,0,0,0.01,0,0,0,1},{0},{0,0,1},0.1,10,0.12};
	CHECK(p.Prepare());return p;
}
double projectedMeshArea(const WaterProjectedMesh::Mesh &mesh,const WaterProjectedMesh::Projection &projection)
{
	double area=0;
	for (size_t i=0;i<mesh.indices.size();i+=3) {
		WaterProjectedMesh::Point p[3];
		for (int j=0;j<3;++j) { CHECK(mesh.indices[i+j]<mesh.vertices.size());CHECK(projection.Transform(mesh.vertices[mesh.indices[i+j]].world,p[j])); }
		const double triangle=(p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);
		CHECK(triangle>0);area+=triangle*0.5;
	}
	return area;
}
}

TEST(water_projected_lattice_covers_visible_plane_once_with_bounded_shared_vertices)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
	const Projection projection=testWaterProjection();Mesh mesh;
	CHECK(Build(polygon,projection,{160,90},mesh));
	CHECK(mesh.vertices.size()<=VertexBound(160,90));
	// Frustum-edge cells retain their centre fan so authored-edge knots can enter without a
	// topology pop. Interior cells still have exactly two triangles; added work is perimeter-only.
	const size_t perimeterCells=2u*160+2u*90-4;
	CHECK(mesh.indices.size()>=160u*90*6 && mesh.indices.size()<=160u*90*6+perimeterCells*6);
	CHECK(mesh.vertices.size()<=161u*91+perimeterCells);
	CHECK(std::abs(projectedMeshArea(mesh,projection)-4*1.12*1.12)<1e-9);
	CHECK(mesh.vertices.size()<mesh.indices.size()/4); // Adjacent cells actually share samples.
	for (const Vertex &vertex:mesh.vertices) {
		CHECK(std::isfinite(vertex.world.x) && std::isfinite(vertex.world.y));
		CHECK(std::abs(vertex.world.x)<=112.000001 && std::abs(vertex.world.y)<=112.000001);
	}
}

TEST(water_projected_integer_cache_preserves_exact_reference_vertices_and_indices)
{
	using namespace WaterProjectedMesh;
	for (int fixture=0;fixture<8;++fixture) for (Resolution resolution:{Resolution{2,2},Resolution{31,17},Resolution{160,90}}) {
		Point corners[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
		if (fixture%3==1) { corners[0]={-95,-170};corners[1]={81,-110};corners[2]={120,92};corners[3]={-64,170}; }
		if (fixture%3==2) { corners[0]={-200,-200};corners[1]={0,-200};corners[2]={0,200};corners[3]={-200,200}; }
		if (fixture&1) std::reverse(corners,corners+4);
		Projection projection=testWaterProjection();
		projection.matrix[2]=fixture*0.013;projection.matrix[5]=fixture*-0.009;
		projection.matrix[6]=fixture*0.000021;projection.matrix[7]=fixture*0.00011;CHECK(projection.Prepare());
		std::vector<BoundaryKnot> knots;
		for (int edge=0;edge<4;++edge) {
			Point a=corners[edge],b=corners[(edge+1)%4];
			if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
			for (int i=0;i<=16;++i) { const double t=double(i)/16;knots.push_back({{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t},edge,i,t}); }
		}
		Mesh reference,fast;
		CHECK(Build(corners,projection,resolution,reference,60000,knots,false));
		CHECK(Build(corners,projection,resolution,fast,60000,knots,true));
		CHECK(reference.cells==fast.cells && reference.indices==fast.indices);
		CHECK(reference.vertices.size()==fast.vertices.size());
		CHECK(reference.transitions.size()==fast.transitions.size() && reference.collars.size()==fast.collars.size());
		if (reference.vertices.size()!=fast.vertices.size()) continue;
		std::unordered_map<WorldKey,float,WorldHash> cachedHeights;
		for (size_t i=0;i<fast.vertices.size();++i) {
			const Vertex &a=reference.vertices[i],&b=fast.vertices[i];
			CHECK(std::memcmp(&a.world,&b.world,sizeof(a.world))==0);
			CHECK(a.boundary==b.boundary && a.knot==b.knot && std::memcmp(&a.edgeParameter,&b.edgeParameter,sizeof(double))==0);
			CHECK(a.transition==b.transition && a.collar==b.collar);
			if (i%17==0) {
				const float x=float(a.world.x),y=float(a.world.y);
				const WorldKey key=SampleKey(x,y,7);
				const float direct=WaterSurfaceModel::EvaluateOceanHeight(x,y,2.3f);
				const auto old=cachedHeights.emplace(key,direct);
				CHECK(direct==old.first->second); // Bypassing the single-patch value cache changes no wave sample.
			}
		}
	}
}

TEST(water_projected_clipping_preserves_reversed_shared_authored_boundaries)
{
	using namespace WaterProjectedMesh;
	const Point left[4]={{-200,-200},{0,-200},{0,200},{-200,200}};
	const Point right[4]={{200,200},{0,200},{0,-200},{200,-200}};
	Projection projection=testWaterProjection();projection.matrix[7]=0.001;CHECK(projection.Prepare());
	Mesh a,b;CHECK(Build(left,projection,{40,24},a));CHECK(Build(right,projection,{40,24},b));
	std::vector<double> ea,eb;
	for (const Vertex &v:a.vertices) if (v.world.x==0 && v.boundary>=0) ea.push_back(v.world.y);
	for (const Vertex &v:b.vertices) if (v.world.x==0 && v.boundary>=0) eb.push_back(v.world.y);
	std::sort(ea.begin(),ea.end());ea.erase(std::unique(ea.begin(),ea.end()),ea.end());
	std::sort(eb.begin(),eb.end());eb.erase(std::unique(eb.begin(),eb.end()),eb.end());
	CHECK(ea.size()>20 && ea.size()==eb.size());
	for (size_t i=0;i<ea.size() && i<eb.size();++i) CHECK(ea[i]==eb[i]);
	CHECK(std::abs(projectedMeshArea(a,projection)+projectedMeshArea(b,projection)-4*1.12*1.12)<1e-9);
}

TEST(water_projected_plane_guard_preserves_authored_nonplanar_levels)
{
	const float level[4]={7,7,7,7},sloped[4]={7,7,7,7.001f},invalid[4]={INFINITY,INFINITY,INFINITY,INFINITY};
	CHECK(WaterProjectedMesh::HasSingleLevel(level));
	CHECK(!WaterProjectedMesh::HasSingleLevel(sloped));
	CHECK(!WaterProjectedMesh::HasSingleLevel(invalid));
}

TEST(water_projected_near_horizon_is_finite_and_clips_real_authored_coverage)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
	Projection projection=testWaterProjection();projection.matrix[7]=0.002;projection.depth={0,0.002,1};projection.nearDistance=0.2;CHECK(projection.Prepare());
	Mesh mesh;CHECK(Build(polygon,projection,{48,32},mesh));CHECK(!mesh.indices.empty());
	for (const Vertex &v:mesh.vertices) {
		CHECK(std::isfinite(v.world.x) && std::isfinite(v.world.y));
		CHECK(std::abs(v.world.x)<=500.000001 && std::abs(v.world.y)<=500.000001);
		CHECK(Distance(projection.depth,v.world)>=0.2-1e-10);
	}
	CHECK(projectedMeshArea(mesh,projection)>0);
	projection.matrix[3]=projection.matrix[4]=projection.matrix[5]=0;CHECK(!projection.Prepare());
	CHECK(!Build(polygon,testWaterProjection(),{192,128},mesh,20)); // Explicit fallback; no partial mesh accepted.
}

TEST(water_projected_boundary_contains_each_retained_displaced_edge_bend)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-100,-100},{0,-100},{0,100},{-100,100}};
	std::vector<BoundaryKnot> knots;
	for (int i=0;i<=16;++i) knots.push_back({{0,-100+i*12.5},1,i,double(i)/16});
	Mesh mesh;CHECK(Build(polygon,testWaterProjection(),{2,2},mesh,60000,knots));
	CHECK(mesh.vertices.size()<=VertexBound(2,2,knots.size()));
	auto knotHeight=[](int i) { return std::sin((-100+i*12.5)*0.12)+i*0.07; };
	auto retainedHeight=[&](double y) {
		const double u=(y+100)/12.5;const int i=(std::max)(0,(std::min)(15,int(u)));
		return knotHeight(i)+(knotHeight(i+1)-knotHeight(i))*(u-i);
	};
	bool included[17]={};double covered=0,maxSkippedBend=0;
	for (const Vertex &v:mesh.vertices) if (v.knot>=0) {
		CHECK(v.knot<=16);included[v.knot]=true;
		CHECK(v.world.y==knots[v.knot].world.y);
	}
	for (bool present:included) CHECK(present);
	for (size_t t=0;t<mesh.indices.size();t+=3) for (int e=0;e<3;++e) {
		const Vertex &a=mesh.vertices[mesh.indices[t+e]],&b=mesh.vertices[mesh.indices[t+(e+1)%3]];
		if (a.world.x!=0 || b.world.x!=0 || a.world.y==b.world.y) continue;
		const double lo=(std::min)(a.world.y,b.world.y),hi=(std::max)(a.world.y,b.world.y);
		covered+=hi-lo;
		for (const BoundaryKnot &k:knots) CHECK(!(k.world.y>lo+1e-10 && k.world.y<hi-1e-10));
		const double midpoint=(lo+hi)*0.5;
		CHECK(std::abs((retainedHeight(lo)+retainedHeight(hi))*0.5-retainedHeight(midpoint))<1e-12);
	}
	CHECK(std::abs(covered-200)<1e-10);
	// Armed control: joining only the projected cell endpoints would visibly skip wave bends.
	for (int i=1;i<8;++i) maxSkippedBend=(std::max)(maxSkippedBend,
		std::abs(knotHeight(i)-(knotHeight(0)+(knotHeight(8)-knotHeight(0))*i/8)));
	CHECK(maxSkippedBend>0.5);
	const Point concave[4]={{-100,-100},{100,-100},{0,-50},{-100,100}};
	CHECK(!Build(concave,testWaterProjection(),{2,2},mesh));
}

TEST(water_projected_sampling_moves_continuously_and_refreshes_for_resize)
{
	using namespace WaterProjectedMesh;
	Projection a=testWaterProjection(),b=a;b.matrix[2]+=1e-7;CHECK(b.Prepare());
	for (int y=-4;y<=4;++y) for (int x=-4;x<=4;++x) {
		Point p,q;CHECK(a.Transform({x*0.2,y*0.2},p,true));CHECK(b.Transform({x*0.2,y*0.2},q,true));
		CHECK(std::abs((q.x-p.x)+0.00001)<1e-10 && std::abs(q.y-p.y)<1e-10);
		CHECK(std::abs(WaterSurfaceModel::EvaluateOceanHeight(float(p.x),float(p.y),1.7f)-
			WaterSurfaceModel::EvaluateOceanHeight(float(q.x),float(q.y),1.7f))<0.0001f);
	}
	const size_t available=WaterSurfaceModel::VertexBudget-50u*65*65;
	const Resolution carrier=ChooseResolution(1280,720,2,available,4*65),resize=ChooseResolution(960,540,2,available,4*65);
	CHECK(carrier.columns==160 && carrier.rows==90);
	CHECK(resize.columns<carrier.columns && resize.rows<carrier.rows);
	for (int patches:{1,2,10,50}) {
		const Resolution r=ChooseResolution(3840,2160,patches,WaterSurfaceModel::VertexBudget);
		CHECK(size_t(patches)*VertexBound(r.columns,r.rows)<=WaterSurfaceModel::VertexBudget);
		CHECK(VertexBound(r.columns,r.rows)<=60000);
	}
	for (int allPatches:{2,52,500,4000}) {
		const int base=WaterSurfaceModel::CellsForPatches(allPatches),flat=(std::max)(1,allPatches/3);
		const size_t legacy=size_t(base+1)*(base+1),retained=size_t(allPatches-flat)*legacy;
		const size_t available=WaterSurfaceModel::VertexBudget-retained,share=available/flat;
		const Resolution r=ChooseResolution(1280,720,flat,available,4*(base+1));
		const size_t bound=VertexBound(r.columns,r.rows,4*(base+1));
		// Projection can fail (budget, singular camera or concavity); its legacy grid remains
		// inside the same share, so an arbitrary mix of accepted and fallback patches is bounded.
		CHECK(legacy<=share);
		CHECK(retained+size_t(flat)*(bound<=share?bound:legacy)<=WaterSurfaceModel::VertexBudget);
	}
}

namespace {
double projectedTestWave(WaterProjectedMesh::Point p) {
	return 1.2*std::sin(p.x*0.037)+0.7*std::cos(p.y*0.049)+0.4*std::sin((p.x+p.y)*0.021);
}
double projectedTestEdge(const WaterProjectedMesh::Point corners[4],int edge,double parameter) {
	using namespace WaterProjectedMesh;
	Point a=corners[edge],b=corners[(edge+1)%4];
	if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
	// The retained mesh is deliberately much coarser than the nonlinear analytic height.
	const double u=parameter*2;const int index=(std::min)(1,int(u));const double t=u-index;
	const Point lo={a.x+(b.x-a.x)*index/2,a.y+(b.y-a.y)*index/2};
	const Point hi={a.x+(b.x-a.x)*(index+1)/2,a.y+(b.y-a.y)*(index+1)/2};
	return projectedTestWave(lo)+(projectedTestWave(hi)-projectedTestWave(lo))*t;
}
double projectedTestCollar(const WaterProjectedMesh::Point corners[4],WaterProjectedMesh::Point p,const WaterProjectedMesh::BoundaryCollar &collar) {
	const double analytic=projectedTestWave(p);double value=analytic;
	for (int i=0;i<collar.count;++i) value+=collar.weight[i]*(projectedTestEdge(corners,collar.edge[i],collar.parameter[i])-analytic);
	return value;
}
std::vector<double> projectedTestHeights(const WaterProjectedMesh::Mesh &mesh,const WaterProjectedMesh::Point corners[4]) {
	using namespace WaterProjectedMesh;
	std::vector<double> heights;
	for (const Vertex &v:mesh.vertices) {
		double height=v.boundary>=0?projectedTestEdge(corners,v.boundary,v.edgeParameter):
			(v.collar>=0?projectedTestCollar(corners,v.world,mesh.collars[v.collar]):projectedTestWave(v.world));
		if (v.transition>=0) {
			const CentreTransition &transition=mesh.transitions[v.transition];double source[3],sum=0;
			for (int i=0;i<3;++i) {
				CHECK(transition.weight[i]>=-1e-10 && transition.weight[i]<=1+1e-10);sum+=transition.weight[i];
				source[i]=projectedTestCollar(corners,transition.source[i],CollarAt(corners,transition.source[i]));
			}
			CHECK(std::abs(sum-1)<1e-10);height=InterpolateCentre(transition,height,source);
		}
		CHECK(std::abs(height)<=2.300001);heights.push_back(height);
	}
	return heights;
}
double projectedTestRenderedHeight(const WaterProjectedMesh::Mesh &mesh,const std::vector<double> &height,WaterProjectedMesh::Point p) {
	using namespace WaterProjectedMesh;
	for (size_t i=0;i<mesh.indices.size();i+=3) {
		const unsigned ia=mesh.indices[i],ib=mesh.indices[i+1],ic=mesh.indices[i+2];
		const Point a=mesh.vertices[ia].world,b=mesh.vertices[ib].world,c=mesh.vertices[ic].world;
		const double determinant=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
		if (std::abs(determinant)<1e-16) continue;
		const double v=((p.x-a.x)*(c.y-a.y)-(p.y-a.y)*(c.x-a.x))/determinant;
		const double w=((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x))/determinant;
		if (v>=-1e-10 && w>=-1e-10 && v+w<=1+1e-10) return height[ia]*(1-v-w)+height[ib]*v+height[ic]*w;
	}
	CHECK(false);return 0;
}
}

TEST(water_projected_actual_triangles_stay_continuous_at_knot_and_full_cell_transitions)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-500,-500},{0,-500},{0,500},{-500,500}};
	std::vector<BoundaryKnot> knots;
	for (int edge=0;edge<4;++edge) {
		Point a=polygon[edge],b=polygon[(edge+1)%4];
		if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
		for (int i=0;i<=2;++i) knots.push_back({{a.x+(b.x-a.x)*i/2,a.y+(b.y-a.y)*i/2},edge,i,i/2.0});
	}
	for (int axis:{0,1}) {
		Projection before=testWaterProjection(),after=before;
		// Y motion moves the only visible retained knot across a lattice line; X motion
		// changes a clipped cell into a full cell. Compare the actual nonlinear triangles.
		before.matrix[axis*3+2]=-1e-7;after.matrix[axis*3+2]=1e-7;
		CHECK(before.Prepare() && after.Prepare());Mesh a,b;
		CHECK(Build(polygon,before,{2,2},a,60000,knots));CHECK(Build(polygon,after,{2,2},b,60000,knots));
		const auto ah=projectedTestHeights(a,polygon),bh=projectedTestHeights(b,polygon);
		for (double y:{-90.0,-50.0,-10.0,10.0,50.0,90.0}) for (double x:{-95.0,-65.0,-35.0,-5.0})
			CHECK(std::abs(projectedTestRenderedHeight(a,ah,{x,y})-projectedTestRenderedHeight(b,bh,{x,y}))<0.00002);
		CHECK(a.vertices.size()<=VertexBound(2,2,knots.size()) && b.vertices.size()<=VertexBound(2,2,knots.size()));
	}
	// Armed controls: both discarded centre height and the old boundary/interior handoff
	// differ materially here. A test comparing only moving XY samples would miss these pops.
	CHECK(std::abs(projectedTestWave({-56,-56})-(projectedTestWave({-112,-112})+projectedTestWave({0,0}))*0.5)>0.1);
	CHECK(std::abs(projectedTestWave({0,50})-projectedTestEdge(polygon,1,0.55))>0.1);
	for (double y:{-501.0,-499.0,0.0,499.0,501.0}) for (double x:{-12.00001,-12.0,-11.99999,-0.00001,0.00001}) {
		const double a=projectedTestCollar(polygon,{x,y},CollarAt(polygon,{x,y}));
		const double b=projectedTestCollar(polygon,{x+1e-7,y},CollarAt(polygon,{x+1e-7,y}));
		CHECK(std::abs(a-b)<0.000001 && std::abs(a)<=2.300001);
	}
}

TEST(water_projected_horizon_reference_rejects_nonconvex_weights_and_preserves_height_bounds)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-500,-0.9},{500,-0.9},{500,500},{-500,500}};
	// The horizon lies inside one lattice row. More than75% of that row is visible, so it
	// attempts the regular-cell transition, but its upper reference corners are behind the eye.
	Projection projection={{1,0,0,0,1.018,0,0,1,1},{0},{0,1,1},0.1,600,0.12};
	CHECK(projection.Prepare());Mesh mesh;CHECK(Build(polygon,projection,{16,90},mesh));
	CHECK(projectedMeshArea(mesh,projection)>0);
	const double bottom=1.12*(2.0*85/90-1),top=1.12*(2.0*86/90-1),visibleTop=1.018*500/501;
	const double centre=(bottom+visibleTop)*0.5;
	Point lo,hi,mid;CHECK(projection.Transform({0,bottom},lo,true));
	CHECK(projection.Transform({0,top},hi,true));CHECK(projection.Transform({0,centre},mid,true));
	CHECK((top-visibleTop)/(top-bottom)<0.25 && Distance(projection.depth,hi)<0);
	const double opposite=(mid.y-lo.y)/(hi.y-lo.y);
	CHECK(opposite<-0.08); // Armed control: the unguarded production formula is non-convex.
	double unsafe[3]={1-opposite,0,opposite};CHECK(!NormalizeCentreWeights(unsafe));
	CHECK((1-opposite)*2.3+opposite*-2.3>2.6); // It can exceed the2.3-unit source bound.
	int guarded=0;
	for (const Vertex &v:mesh.vertices) if (v.boundary<0 && std::abs(v.world.y-mid.y)<1e-7) {
		++guarded;CHECK(v.transition<0);
	}
	CHECK(guarded>0);
	const auto heights=projectedTestHeights(mesh,polygon);
	for (double height:heights) CHECK(std::isfinite(height) && std::abs(height)<=2.300001);
	CHECK(mesh.vertices.size()<=VertexBound(16,90));
	double roundoff[3]={-1e-12,0.4,0.600000000001};CHECK(NormalizeCentreWeights(roundoff));
	CHECK(roundoff[0]==0 && std::abs(roundoff[0]+roundoff[1]+roundoff[2]-1)<1e-14);
	double nonfinite[3]={0.5,0.5,NAN};CHECK(!NormalizeCentreWeights(nonfinite));
}

TEST(water_vessel_motion_tracks_scripted_transforms_and_braking)
{
	using namespace WaterVesselMotion;
	State state;
	CHECK(Update(state,{0,0},7,100,false,{},30).x==0);
	// A scripted vessel can have zero physics velocity while its displayed hull moves.
	// Braking must likewise follow that transform instead of forcing its wake speed to zero.
	for (unsigned step=1;step<=60;++step) {
		const Point velocity=Update(state,{float(step),-0.5f*step},7,100+step,false,{},30);
		const float response=1-std::exp(-float(step)/30/SmoothingSeconds);
		CHECK(std::abs(velocity.x-30*response)<0.00002f);
		CHECK(std::abs(velocity.y+15*response)<0.00002f);
	}
	CHECK(state.velocity.x>29.99f); // Armed against retaining the zero physics-only velocity.
	const Point moving=state.velocity;
	for (unsigned step=1;step<=30;++step) {
		const Point velocity=Update(state,{60,-30},7,160+step,false,{100,100},30);
		const float decay=std::exp(-float(step)/30/SmoothingSeconds);
		CHECK(std::abs(velocity.x-moving.x*decay)<0.00002f);
		CHECK(std::abs(velocity.y-moving.y*decay)<0.00002f);
	}
	CHECK(std::abs(state.velocity.x)<0.04f); // Stale nonzero physics cannot sustain a stopped wake.
}

TEST(water_vessel_motion_is_stable_during_pause_and_repeated_render_frames)
{
	using namespace WaterVesselMotion;
	State state;
	Update(state,{0,0},7,100,false,{30,-15},30);
	const Point original=state.velocity;
	for (int draw=0;draw<100;++draw) {
		const Point sameFrame=Update(state,{0.99f,-0.45f},7,100,false,{0,0},30);
		CHECK(sameFrame.x==original.x && sameFrame.y==original.y);
		CHECK(state.frame==100 && state.position.x==0 && state.position.y==0);
		const Point frozen=Update(state,{9,9},7,101,true,{0,0},30);
		CHECK(frozen.x==original.x && frozen.y==original.y);
		CHECK(state.frame==100 && state.position.x==0 && state.position.y==0);
	}
	const Point resumed=Update(state,{1,-0.5f},7,101,false,{},30);
	CHECK(std::abs(resumed.x-30)<0.00001f && std::abs(resumed.y+15)<0.00001f);
	// Equal elapsed simulation time gives the same filtered velocity when render visibility
	// samples every frame, every other frame, or every third frame; extra render calls do not age it.
	Point results[3];
	for (unsigned cadence=1;cadence<=3;++cadence) {
		State sampled;Update(sampled,{0,0},7,0,false,{},30);
		for (unsigned frame=cadence;frame<=60;frame+=cadence) {
			results[cadence-1]=Update(sampled,{float(frame),float(frame)*0.5f},7,frame,false,{},30);
			Update(sampled,{float(frame)+0.4f,float(frame)*0.5f+0.2f},7,frame,false,{},30);
		}
	}
	for (const Point velocity:results) {
		CHECK(std::abs(velocity.x-results[0].x)<0.00002f);
		CHECK(std::abs(velocity.y-results[0].y)<0.00002f);
	}
}

TEST(water_vessel_motion_rejects_teleports_stale_history_and_object_replacement)
{
	using namespace WaterVesselMotion;
	auto moving=[]() { State state;Update(state,{0,0},7,100,false,{30,10},30);return state; };
	State state=moving();
	Point result=Update(state,{33,0},7,101,false,{90,0},30);
	CHECK(result.x==0 && result.y==0 && state.position.x==33 && state.frame==101);
	result=Update(state,{34,0},7,102,false,{},30);
	CHECK(result.x>0 && result.x<30); // A teleport anchors, then genuine movement can resume.
	state=moving();result=Update(state,{16,0},7,116,false,{90,0},30);
	CHECK(result.x==90 && result.y==0); // Camera reentry reseeds from current physics, never old displacement.
	state=moving();result=Update(state,{16,0},7,116,false,{NAN,0},30);
	CHECK(result.x==0 && result.y==0); // Missing/invalid physics cannot resurrect stale motion.
	state=moving();result=Update(state,{16,0},7,116,false,{300,400},30);
	CHECK(result.x==150 && result.y==200); // The reentry seed obeys the same vector speed bound.
	state=moving();result=Update(state,{1000,0},7,116,false,{90,0},30);
	CHECK(result.x==0 && result.y==0); // A teleport still resets, even after a visibility gap.
	state=moving();result=Update(state,{15,0},7,115,false,{},30);
	CHECK(result.x>0); // The inclusive half-second limit still observes real motion.
	state=moving();result=Update(state,{1,0},8,101,false,{90,0},30);
	CHECK(result.x==0 && result.y==0 && state.level==8);
	state=moving();result=Update(state,{1,0},7,99,false,{90,0},30);
	CHECK(result.x==0 && result.y==0 && state.frame==99);
	state=moving();state=State();result=Update(state,{1000,2000},7,101,false,{},30);
	CHECK(result.x==0 && result.y==0); // Model/ID cache replacement cannot inherit old velocity.
}

TEST(water_vessel_motion_uses_the_actual_first_render_phase)
{
	using namespace WaterVesselMotion;
	const float phases[]={0.125f,0.875f,0.25f,0.75f,0.5f};
	State sampled,integerTime;
	float largestControlError=0;
	for (unsigned frame=100;frame<160;++frame) {
		const float phase=phases[(frame-100)%5];
		// A constant 30,-15 unit/s ship, rendered at a different interpolation phase on
		// each tick. Binary phases keep this independent reference free of roundoff noise.
		const float sampleTime=float(frame-1)+phase;
		const Point position={sampleTime,-sampleTime*0.5f};
		const Point velocity=Update(sampled,position,7,frame,false,{30,-15},30,phase);
		CHECK(std::abs(velocity.x-30)<0.00001f && std::abs(velocity.y+15)<0.00001f);
		CHECK(sampled.phase==phase && sampled.frame==frame);
		const Point control=Update(integerTime,position,7,frame,false,{30,-15},30);
		largestControlError=(std::max)(largestControlError,std::abs(control.x-30));
		// Additional draws and frozen passes do not replace this tick's sampled phase.
		const Point repeated=Update(sampled,{sampleTime+0.1f,0},7,frame,false,{},30,0.99f);
		const Point frozen=Update(sampled,{1000,1000},7,frame+1,true,{},30,0.75f);
		CHECK(repeated.x==velocity.x && frozen.x==velocity.x);
		CHECK(sampled.phase==phase && sampled.position.x==position.x && sampled.frame==frame);
	}
	CHECK(largestControlError>4.0f); // Armed control: integer-only timing invents acceleration.
	// Models snapped to their current tick are sampled at phase1, even when the global
	// renderer alpha differs. Switching between a snapped and blended model stays correct.
	State transitions;
	Update(transitions,{99.25f,0},7,100,false,{30,0},30,0.25f);
	Point velocity=Update(transitions,{101,0},7,101,false,{},30,1);
	CHECK(velocity.x==30 && transitions.phase==1);
	velocity=Update(transitions,{101.5f,0},7,102,false,{},30,0.5f);
	CHECK(velocity.x==30 && transitions.phase==0.5f);
}

TEST(water_vessel_motion_handles_zero_elapsed_phase_and_invalid_phase)
{
	using namespace WaterVesselMotion;
	State state;
	Update(state,{5,3},7,100,false,{30,-15},30,1);
	Point velocity=Update(state,{5,3},7,101,false,{},30,0);
	CHECK(velocity.x==30 && velocity.y==-15 && state.frame==101 && state.phase==0);
	velocity=Update(state,{6.5f,2.25f},7,102,false,{},30,0.5f);
	CHECK(velocity.x==30 && velocity.y==-15);
	Update(state,{8,1.5f},7,103,false,{},30,1);
	velocity=Update(state,{40,10},7,104,false,{100,100},30,0);
	CHECK(velocity.x==0 && velocity.y==0); // Changed transform at identical time cannot be divided by zero.
	for (float invalid:{float(NAN),float(INFINITY),float(-INFINITY)}) {
		State bad;velocity=Update(bad,{0,0},7,0,false,{10,10},30,invalid);
		CHECK(velocity.x==0 && velocity.y==0 && !bad.initialized);
	}
	State clamped;
	Update(clamped,{0,0},7,0,false,{},30,-0.01f);CHECK(clamped.phase==0);
	Update(clamped,{2,0},7,1,false,{},30,1.01f);CHECK(clamped.phase==1);
	// The history threshold uses real displayed sample time too, not an integer tick count.
	State recent;Update(recent,{0,0},7,100,false,{30,0},30,0.875f);
	velocity=Update(recent,{14.25f,0},7,115,false,{90,0},30,0.125f);
	CHECK(velocity.x==30);
	State stale;Update(stale,{0,0},7,100,false,{30,0},30,0.125f);
	velocity=Update(stale,{15.75f,0},7,115,false,{90,0},30,0.875f);
	CHECK(velocity.x==90);
}

TEST(water_vessel_motion_is_finite_vector_bounded_and_rotation_equivariant)
{
	using namespace WaterVesselMotion;
	State state;
	Point velocity=Update(state,{0,0},7,0,false,{300,400},30);
	CHECK(std::abs(velocity.x-150)<0.00001f && std::abs(velocity.y-200)<0.00001f);
	for (unsigned frame=1;frame<=60;++frame) {
		velocity=Update(state,{10.0f*frame,10.0f*frame},7,frame,false,{},30);
		CHECK(std::isfinite(velocity.x) && std::isfinite(velocity.y));
		CHECK(double(velocity.x)*velocity.x+double(velocity.y)*velocity.y<=double(MaxSpeed)*MaxSpeed+0.01);
	}
	for (float invalid:{float(NAN),float(INFINITY),float(-INFINITY)}) {
		State bad;velocity=Update(bad,{0,0},7,0,false,{invalid,1},30);
		CHECK(velocity.x==0 && velocity.y==0);
		velocity=Update(state,{invalid,0},7,61,false,{},30);
		CHECK(velocity.x==0 && velocity.y==0 && !state.initialized);
		velocity=Update(state,{0,0},invalid,61,false,{},30);
		CHECK(velocity.x==0 && velocity.y==0 && !state.initialized);
		velocity=Update(state,{0,0},7,61,false,{},invalid);
		CHECK(velocity.x==0 && velocity.y==0 && !state.initialized);
	}
	State straight,rotated;
	Update(straight,{0,0},7,0,false,{},30);Update(rotated,{100,-200},7,0,false,{},30);
	for (unsigned frame=1;frame<=60;++frame) {
		const Point p={float(frame),0.125f*float(frame*frame)};
		const Point a=Update(straight,p,7,frame,false,{},30);
		const Point b=Update(rotated,{100-p.y,-200+p.x},7,frame,false,{},30);
		CHECK(std::abs(a.x-b.y)<0.00002f && std::abs(a.y+b.x)<0.00002f);
	}
}

TEST(water_fft_metadata_preserves_shared_edges_and_floor_and_hull_bounds)
{
	using namespace WaterProjectedMesh;
	using namespace WaterFftGeometry;
	const Point left[4]={{-500,-500},{0,-500},{0,500},{-500,500}};
	const Point right[4]={{0,500},{0,-500},{500,-500},{500,500}};
	const Projection projection=testWaterProjection();SeamFade a,b;
	CHECK(a.Prepare(left,projection,{16,16}) && b.Prepare(right,projection,{16,16}));
	for (int i=-100;i<=100;++i) {
		CHECK(a.Weight({0,double(i)},1)==0 && b.Weight({0,double(i)},1)==0);
		CHECK(a.Weight({-75,double(i)},0)==0);
	}
	CHECK(a.Weight({-75,0},1)==1);
	CHECK(a.Weight({-75,0},0.37f)==0.37f);
	CHECK(InnerWorldCollar>MaxHorizontalDisplacement);
	for (int i=0;i<500;++i) {
		const double distance=i*0.1;
		CHECK(MaxHorizontalDisplacement*a.Weight({-distance,0},1)<=distance+1e-6);
	}
	for (float depth:{-4.0f,0.0f,0.1f,0.15f,0.16f,2.25f,7.0f,30.0f})
		for (float damping:{-1.0f,0.0f,0.17f,0.5f,1.0f,2.0f}) {
			const float safe=SafeDamping(damping,depth);
			CHECK(std::isfinite(safe) && safe>=0 && safe<=1);
			CHECK(safe*MaxVerticalDisplacement<=(std::max)(0.0f,depth-FloorClearance)+1e-6f);
			if (damping<=0 || depth<=FloorClearance) CHECK(safe==0);
		}
	CHECK(SafeDamping(NAN,3)==0 && SafeDamping(1,NAN)==0);
	// The existing nine-unit hull fade also bounds lateral travel: an eight-unit maximum
	// offset cannot carry a sample across the waterline it was attenuated against.
	for (int i=0;i<=900;++i) {
		const float distance=i*0.01f;
		CHECK(MaxHorizontalDisplacement*WaterSurfaceModel::ShoreDamping(distance)<=distance+1e-6f);
	}
}

TEST(water_fft_hull_free_metadata_preserves_original_vertices_and_clearance)
{
	using namespace WaterFftGeometry;
	// Moving only the hull minimum to the two GPU stages must preserve the original
	// vertex displacement, including the authored shallow sea's stricter floor envelope.
	for (float depth:{-2.0f,0.15f,0.3f,2.25f,7.0f,30.0f})
		for (float offshore:{0.0f,0.125f,0.5f,0.875f,1.0f})
			for (float obstacle:{0.0f,0.1f,0.37f,0.8f,1.0f})
				for (int step=-10;step<=120;++step) {
					const float distance=step*0.1f;
					const float hull=1.0f+(WaterSurfaceModel::ShoreDamping(distance)-1.0f)*offshore;
					const float freeDamping=(std::min)(WaterSurfaceModel::TerrainDamping(depth,offshore),obstacle);
					const float original=SafeDamping((std::min)(freeDamping,hull),depth);
					const float separated=(std::min)(SafeDamping(freeDamping,depth),hull);
					CHECK(original==separated && separated>=0 && separated<=1);
					CHECK(separated*MaxVerticalDisplacement<=(std::max)(0.0f,depth-FloorClearance)+1e-6f);
					if (offshore==1 && distance>=0)
						CHECK(MaxHorizontalDisplacement*separated<=distance+1e-6f);
					if (offshore==0 || distance>=9)
						CHECK(separated==SafeDamping(freeDamping,depth));
				}
	// An overlapping second hull remains an ordered minimum, without dividing out a
	// zero hull factor or losing a lower floor/non-vessel obstacle constraint.
	for (int step=0;step<=90;++step) {
		const float a=WaterSurfaceModel::ShoreDamping(step*0.1f);
		const float b=WaterSurfaceModel::ShoreDamping((90-step)*0.1f);
		const float original=SafeDamping((std::min)(0.8f,(std::min)(a,b)),2.25f);
		const float separated=(std::min)(SafeDamping(0.8f,2.25f),(std::min)(a,b));
		CHECK(original==separated);
	}
}

TEST(water_fft_seam_fade_is_continuous_under_camera_motion_resize_and_horizon_crossing)
{
	using namespace WaterProjectedMesh;
	using namespace WaterFftGeometry;
	const Point polygon[4]={{-500,-500},{0,-500},{0,500},{-500,500}};
	for (Resolution resolution:{Resolution{16,16},Resolution{160,90},Resolution{192,128}}) {
		Projection p=testWaterProjection(),q=p;q.matrix[2]+=1e-7;CHECK(q.Prepare());SeamFade a,b;
		CHECK(a.Prepare(polygon,p,resolution) && b.Prepare(polygon,q,resolution));
		for (int i=0;i<=1000;++i) {
			const Point point={-i*0.1,1.3};const float w=a.Weight(point,0.8f);
			CHECK(w>=0 && w<=0.8f && std::isfinite(w));
			CHECK(std::abs(w-b.Weight(point,0.8f))<1e-6f);
			CHECK(std::abs(w-a.Weight({point.x-1e-7,point.y},0.8f))<1e-6f);
			if (i>0) CHECK(w+1e-7f>=a.Weight({point.x+0.1,point.y},0.8f));
		}
	}
	// An authored endpoint is exactly on the projective horizon. Moving it from one side
	// to the other must not turn the entire interior field off for a single camera frame.
	const Point horizonPolygon[4]={{-500,-1},{500,-1},{500,500},{-500,500}};
	float previous=-1;
	for (double shift:{-1e-9,0.0,1e-9}) {
		Projection p={{0.01,0,0,0,1.018,0,0,1,1+shift},{0},{0,1,1+shift},0.1,600,0.12};
		CHECK(p.Prepare());SeamFade fade;CHECK(fade.Prepare(horizonPolygon,p,{160,90}));
		const float weight=fade.Weight({0,15},1);CHECK(std::isfinite(weight) && weight>0);
		if (previous>=0) CHECK(std::abs(weight-previous)<1e-6f);previous=weight;
	}
}

TEST(water_fft_actual_boundary_triangles_keep_the_analytic_transition_continuous)
{
	using namespace WaterProjectedMesh;
	using namespace WaterFftGeometry;
	const Point polygon[4]={{-500,-500},{0,-500},{0,500},{-500,500}};
	std::vector<BoundaryKnot> knots;
	for (int edge=0;edge<4;++edge) {
		Point a=polygon[edge],b=polygon[(edge+1)%4];if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
		for (int i=0;i<=2;++i) knots.push_back({{a.x+(b.x-a.x)*i/2,a.y+(b.y-a.y)*i/2},edge,i,i/2.0});
	}
	for (int axis:{0,1}) {
		Projection p=testWaterProjection(),q=p;p.matrix[axis*3+2]=-1e-7;q.matrix[axis*3+2]=1e-7;
		CHECK(p.Prepare() && q.Prepare());Mesh a,b;
		CHECK(Build(polygon,p,{16,16},a,60000,knots) && Build(polygon,q,{16,16},b,60000,knots));
		SeamFade fa,fb;CHECK(fa.Prepare(polygon,p,{16,16}) && fb.Prepare(polygon,q,{16,16}));
		int guardedTotal=0;
		auto displaced=[&](const Mesh &mesh,const SeamFade &fade) {
			auto height=projectedTestHeights(mesh,polygon);int active=0;
			for (size_t i=0;i<mesh.vertices.size();++i) {
				const Vertex &v=mesh.vertices[i];const float weight=fade.Weight(v.world,1);
				if (v.boundary>=0) CHECK(weight==0);
				if (v.transition>=0 && v.world.x>-20 && std::abs(v.world.y)<90) {
					++guardedTotal;CHECK(weight==0);
					for (Point source:mesh.transitions[v.transition].source) CHECK(fade.Weight(source,1)==0);
				}
				const double fft=1.4*std::cos(v.world.x*0.18)+0.6*std::sin(v.world.y*0.11);
				if (weight>0) { ++active; height[i]+=(fft-height[i])*weight; }
				CHECK(std::abs(height[i])<=2.300001);
			}
			CHECK(active>0);return height;
		};
		const auto ah=displaced(a,fa),bh=displaced(b,fb);
		CHECK(guardedTotal>0);
		for (double y:{-80.0,-35.0,-5.0,5.0,35.0,80.0}) for (double x:{-95.0,-75.0,-35.0,-5.0})
			CHECK(std::abs(projectedTestRenderedHeight(a,ah,{x,y})-projectedTestRenderedHeight(b,bh,{x,y}))<0.00003);
	}
}

TEST(water_ocean_height_and_slopes_include_warp_and_packet_derivatives)
{
	using namespace WaterSurfaceModel;
	for (float time:{0.0f,1.73f,31.0f,139.7f}) for (int i=0;i<180;++i) {
		const float x=-190.0f+i*3.713f,y=81.0f-i*2.419f,epsilon=0.015625f;
		const Sample wave=EvaluateOcean(x,y,time);
		const float dx=(EvaluateOcean(x+epsilon,y,time).height-EvaluateOcean(x-epsilon,y,time).height)/(2*epsilon);
		const float dy=(EvaluateOcean(x,y+epsilon,time).height-EvaluateOcean(x,y-epsilon,time).height)/(2*epsilon);
		CHECK(std::abs(wave.dx-dx)<0.002f);
		CHECK(std::abs(wave.dy-dy)<0.002f);
		CHECK(std::isfinite(wave.height) && std::abs(wave.height)<=OceanMaxDisplacement);
	}
	CHECK(MaxDisplacement>=LegacyMaxDisplacement && MaxDisplacement>=OceanMaxDisplacement);
	std::string shader;AppendHLSL(shader);
	CHECK(shader.find("float3 waterOceanWave")!=std::string::npos);
	CHECK(shader.find("envelopeSlope")!=std::string::npos);
}

TEST(water_ocean_zero_classification_preserves_existing_waves_and_damping_exactly)
{
	using namespace WaterSurfaceModel;
	for (int i=0;i<100;++i) {
		const float x=i*19.3f-180,y=i*-7.13f,time=i*0.371f;
		const Sample original=Evaluate(x,y,time),unchanged=EvaluateBlended(x,y,time,{0,0,0});
		CHECK(memcmp(&original,&unchanged,sizeof(Sample))==0);
		const Sample ocean=EvaluateOcean(x,y,time),full=EvaluateBlended(x,y,time,{1,0,0});
		CHECK(memcmp(&ocean,&full,sizeof(Sample))==0);
		const float depth=i*0.137f-1;
		CHECK(TerrainDamping(depth,0)==ShoreDamping(depth));
	}
	const std::uint8_t pool[9]={0,0,0,0,0,0,0,0,0};
	const Sample enclosed=SampleOffshore(pool,3,3,0.3f,1.2f,10);
	CHECK(enclosed.height==0 && enclosed.dx==0 && enclosed.dy==0);
}

TEST(water_ocean_cpu_field_sampling_matches_bilinear_texel_centres_and_padding)
{
	using namespace WaterSurfaceModel;
	const std::uint8_t field[9]={0,64,255,16,96,223,48,128,191};
	for (int y=0;y<3;++y) for (int x=0;x<3;++x)
		CHECK(SampleOffshore(field,3,3,float(x),float(y),10).height==field[y*3+x]/255.0f);
	for (int iy=0;iy<2;++iy) for (int ix=0;ix<2;++ix) for (float fy:{0.17f,0.63f}) for (float fx:{0.23f,0.71f}) {
		const float gx=ix+fx,gy=iy+fy,epsilon=0.001f;
		const Sample sample=SampleOffshore(field,3,3,gx,gy,10);
		const float independent=(field[iy*3+ix]*(1-fx)*(1-fy)+field[iy*3+ix+1]*fx*(1-fy)
			+field[(iy+1)*3+ix]*(1-fx)*fy+field[(iy+1)*3+ix+1]*fx*fy)/255.0f;
		CHECK(std::abs(sample.height-independent)<0.000001f);
		CHECK(std::abs(sample.dx-(SampleOffshore(field,3,3,gx+epsilon,gy,10).height-
			SampleOffshore(field,3,3,gx-epsilon,gy,10).height)/(20*epsilon))<0.00001f);
		CHECK(std::abs(sample.dy-(SampleOffshore(field,3,3,gx,gy+epsilon,10).height-
			SampleOffshore(field,3,3,gx,gy-epsilon,10).height)/(20*epsilon))<0.00001f);
	}
	const Sample left=SampleOffshore(field,3,3,-100,1.5f,10),right=SampleOffshore(field,3,3,100,1.5f,10);
	CHECK(left.dx==0 && right.dx==0);
	CHECK(left.height==SampleOffshore(field,3,3,0,1.5f,10).height);
	CHECK(right.height==SampleOffshore(field,3,3,2,1.5f,10).height);
	CHECK(SampleOffshore(NULL,3,3,1,1,10).height==0);
}

TEST(water_ocean_transition_normals_include_spatial_blend_and_attenuation)
{
	using namespace WaterSurfaceModel;
	auto blend=[](float x,float y) { return Sample{0.45f+x*0.001f+y*0.0005f,0.001f,0.0005f}; };
	auto damping=[&](float x,float y) { return TerrainDamping(6.0f+x*0.01f-y*0.005f,blend(x,y).height); };
	auto height=[&](float x,float y) { return EvaluateBlended(x,y,3.17f,blend(x,y)).height*damping(x,y); };
	for (int i=0;i<80;++i) {
		const float x=-130+i*3.1f,y=53-i*1.7f,epsilon=0.015625f;
		const Sample wave=EvaluateBlended(x,y,3.17f,blend(x,y));
		const float dx=wave.dx*damping(x,y)+wave.height*(damping(x+epsilon,y)-damping(x-epsilon,y))/(2*epsilon);
		const float dy=wave.dy*damping(x,y)+wave.height*(damping(x,y+epsilon)-damping(x,y-epsilon))/(2*epsilon);
		CHECK(std::abs(dx-(height(x+epsilon,y)-height(x-epsilon,y))/(2*epsilon))<0.003f);
		CHECK(std::abs(dy-(height(x,y+epsilon)-height(x,y-epsilon))/(2*epsilon))<0.003f);
	}
}

TEST(water_ocean_displacement_stays_above_authored_floor_and_uses_ship_footprint)
{
	using namespace WaterSurfaceModel;
	for (int d=0;d<=2000;++d) for (int o=0;o<=20;++o) {
		const float depth=d*0.01f,offshore=o/20.0f,attenuation=TerrainDamping(depth,offshore);
		const float bound=LegacyMaxDisplacement+(OceanMaxDisplacement-LegacyMaxDisplacement)*offshore;
		CHECK(attenuation>=0 && attenuation<=1);
		CHECK(attenuation*bound<=depth+0.00001f);
		if (o==20 && depth>=0.15f) CHECK(attenuation*bound<=depth-0.15f+0.00001f);
	}
	CHECK(TerrainDamping(2.25f,1)>TerrainDamping(2.25f,0));
	CHECK(TerrainDamping(OceanMaxDisplacement+0.2f,1)==1);
	CHECK(BoxDamping(0,0,112,74)==0);
	CHECK(BoxDamping(112,74,112,74)==0);
	CHECK(BoxDamping(0,84,112,74)==1); // carrier side: legacy radius134 still flattened here
	CHECK(ShoreDamping(84-134.24f)==0);
	CHECK(BoxDamping(122,0,112,74)==1);
	for (int i=0;i<90;++i) CHECK(BoxDamping(112+i*0.1f,0,112,74)<=BoxDamping(112+(i+1)*0.1f,0,112,74));
}

TEST(water_ocean_obstacle_metadata_preserves_opacity_and_lod_profile)
{
	using namespace WaterSurfaceModel;
	const std::uint32_t colour=0x9e357abdu;
	for (int i=0;i<=1000;++i) {
		const float obstacle=i/1000.0f;
		const std::uint32_t encoded=EncodeObstacleAttenuation(colour,obstacle);
		CHECK((encoded&0xff00ffffu)==(colour&0xff00ffffu));
		CHECK(std::abs(float((encoded>>16)&255)/255.0f-obstacle)<=0.5f/255.0f+0.000001f);
		const std::uint32_t a=EncodeObstacleAttenuation(colour,0.13f),b=EncodeObstacleAttenuation(colour,0.89f);
		for (float morph:{0.0f,0.25f,0.75f,1.0f}) {
			const std::uint32_t mixed=MorphColour(encoded,a,b,morph);
			CHECK((mixed&0xff00ffffu)==(colour&0xff00ffffu));
			CHECK(std::abs(float((mixed>>16)&255)/255.0f-MorphCoordinate(obstacle,0.13f,0.89f,morph))<=1.0f/255.0f+0.000001f);
		}
	}
	CHECK(((EncodeObstacleAttenuation(colour,-1)>>16)&255)==0);
	CHECK(((EncodeObstacleAttenuation(colour,2)>>16)&255)==255);
}

TEST(water_ocean_lod_preserves_budget_and_morphs_continuously_with_shared_topology)
{
	using namespace WaterSurfaceModel;
	for (int height=0;height<2000;++height) {
		const MeshLOD old=LODForCameraHeight(128,float(height)),river=LODForView(128,float(height),0);
		CHECK(old.cells==river.cells && old.morph==river.morph);
		const MeshLOD ocean=LODForView(128,float(height),3000);
		CHECK(ocean.cells>=river.cells && ocean.cells<=128 && ocean.morph>=0 && ocean.morph<=1);
	}
	for (int patches:{1,3,20,33,80,200}) {
		const int base=CellsForPatches(patches);
		for (float height:{117.0f,310.0f,480.0f,900.0f}) {
			const MeshLOD ocean=LODForView(base,height,3000);
			CHECK(patches*(ocean.cells+1)*(ocean.cells+1)<=VertexBudget);
		}
	}
	for (float threshold:{720.0f,1080.0f,1620.0f,2430.0f}) {
		const MeshLOD below=LODForView(128,threshold-0.001f,3000),above=LODForView(128,threshold+0.001f,3000);
		const auto fine=testLODGrid(below.cells,below.morph,true),coarse=testLODGrid(above.cells,above.morph,true);
		for (int j=0;j<12;++j) for (int i=0;i<12;++i) {
			const float u=(i+0.17f)/12,v=(j+0.63f)/12;
			checkLODVertexNear(testLODSample(fine,below.cells,u,v),testLODSample(coarse,above.cells,u,v),0.0003f);
		}
	}
}

TEST(water_ocean_blended_displaced_shared_edges_are_exact_through_all_morphs)
{
	using namespace WaterSurfaceModel;
	std::vector<std::uint8_t> field(256*96);
	for (int y=0;y<96;++y) for (int x=0;x<256;++x) field[y*256+x]=std::uint8_t(x);
	const Point a={-200,17,12},b={2100,533,12},camera={500,200,110};
	auto vertex=[&](Point p) {
		const Sample offshore=SampleOffshore(field.data(),256,96,p.x/10,p.y/10,10);
		const float attenuation=TerrainDamping((p.y-30)*0.07f,offshore.height);
		p.z+=EvaluateBlended(p.x,p.y,1.73f,offshore).height*attenuation;
		return LODTestVertex{p,attenuation,12};
	};
	for (int cells=8;cells<=MeshCells;cells*=2) {
		std::vector<LODTestVertex> forward(cells+1),reverse(cells+1);
		for (int i=0;i<=cells;++i) {
			forward[i]=vertex(EdgePoint(a,b,float(i)/cells,camera));
			reverse[i]=vertex(EdgePoint(b,a,float(i)/cells,camera));
		}
		for (float amount:{0.0f,0.17f,0.5f,0.93f,1.0f}) for (int i=0;i<=cells;++i) {
			const CoarsePair fp=CoarsePairForVertex(i,0,cells),rp=CoarsePairForVertex(cells-i,0,cells);
			const LODTestVertex f=testLODMorph(forward[i],forward[fp.first],forward[fp.second],amount);
			const LODTestVertex r=testLODMorph(reverse[cells-i],reverse[rp.first],reverse[rp.second],amount);
			CHECK(memcmp(&f,&r,sizeof(f))==0);
		}
	}
}

TEST(water_ocean_height_only_evaluation_is_bit_identical_to_the_full_model)
{
	using namespace WaterSurfaceModel;
	for (float time:{0.0f,3.17f,97.5f,1220.3f}) for (int i=0;i<1200;++i) {
		const float x=-4395.0f+i*7.731f,y=3526.0f-i*5.377f;
		CHECK(EvaluateOceanHeight(x,y,time)==EvaluateOcean(x,y,time).height);
		for (const Sample offshore: {Sample{0,0,0},Sample{0.25f,0.01f,-0.005f},Sample{1,0,0},Sample{1,0.001f,0}})
			CHECK(EvaluateBlendedHeight(x,y,time,offshore)==EvaluateBlended(x,y,time,offshore).height);
	}
}

TEST(water_ocean_obstacle_broadphase_only_rejects_fully_unattenuated_samples)
{
	using namespace WaterSurfaceModel;
	for (float major:{3.0f,50.0f,112.0f}) for (float minor:{2.0f,30.0f,74.0f}) {
		const float radius=std::sqrt(major*major+minor*minor);
		CHECK(!BeyondObstacleFade(0,0,radius));
		CHECK(!BeyondObstacleFade(radius+8.9f,0,radius));
		CHECK(BeyondObstacleFade(radius+20,0,radius));
		for (int i=0;i<100;++i) for (float margin:{8.0f,9.0f,9.999f,10.001f,50.0f}) {
			const float angle=i*0.37f,orientation=i*0.83f;
			const float dx=std::cos(angle)*(radius+margin),dy=std::sin(angle)*(radius+margin);
			if (!BeyondObstacleFade(dx,dy,radius)) continue;
			CHECK(ShoreDamping(std::sqrt(dx*dx+dy*dy)-radius)==1);
			CHECK(BoxDamping(dx*std::cos(orientation)+dy*std::sin(orientation),
				-dx*std::sin(orientation)+dy*std::cos(orientation),major,minor)==1);
		}
	}
}

TEST(water_ocean_analytic_laplacian_matches_independent_height_curvature)
{
	using namespace WaterSurfaceModel;
	for (float time:{0.0f,3.17f,19.7f}) for (int i=0;i<120;++i) {
		const float x=-61.0f+i*1.073f,y=43.0f-i*0.917f,epsilon=0.125f;
		const OceanSurface surface=EvaluateOceanSurface(x,y,time);
		const Sample wave=EvaluateOcean(x,y,time);
		CHECK(surface.height==wave.height && surface.dx==wave.dx && surface.dy==wave.dy);
		auto central=[&](float step) {
			return (EvaluateOcean(x+step,y,time).height+EvaluateOcean(x-step,y,time).height
				+EvaluateOcean(x,y+step,time).height+EvaluateOcean(x,y-step,time).height
				-4*surface.height)/(step*step);
		};
		// Richardson extrapolation removes the O(h^2) truncation of the shortest ripple modes,
		// without reducing h until float subtraction loses the small curvature signal.
		const float numerical=(4*central(epsilon)-central(2*epsilon))/3;
		CHECK(std::isfinite(surface.laplacian));
		CHECK(std::abs(surface.laplacian-numerical)<0.0025f);
		auto gradientDifference=[&](float step) {
			return (EvaluateOcean(x+step,y,time).dx-EvaluateOcean(x-step,y,time).dx
				+EvaluateOcean(x,y+step,time).dy-EvaluateOcean(x,y-step,time).dy)/(2*step);
		};
		// The gradient stencil also needs its h^2 term removed: at h=.125 the fastest ripples
		// biased three fixtures by .008–.009 although the analytic/double-height error was <2e-5.
		const float derivative=(4*gradientDifference(epsilon*0.5f)-gradientDifference(epsilon))/3;
		CHECK(std::abs(surface.laplacian-derivative)<0.0005f);
	}
	std::string hlsl=OceanShaderSource();
	CHECK(hlsl.find("float4 waterOceanSurface")!=std::string::npos);
	CHECK(hlsl.find("return waterOceanSurface(pxy,seconds).xyz")!=std::string::npos);
}
