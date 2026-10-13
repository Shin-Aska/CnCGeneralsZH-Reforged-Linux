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
#include <cmath>

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
std::vector<LODTestVertex> testLODGrid(int cells,float amount)
{
	using namespace WaterSurfaceModel;
	const Point corners[4]={{0,0,12},{600,10,12},{520,300,12},{-100,270,12}},camera={180,75,200};
	std::vector<LODTestVertex> grid((cells+1)*(cells+1));
	for (int j=0;j<=cells;++j) for (int i=0;i<=cells;++i) {
		const float u=float(i)/cells,v=float(j)/cells;
		const Point edges[4]={EdgePoint(corners[0],corners[1],u,camera),EdgePoint(corners[1],corners[2],v,camera),
			EdgePoint(corners[3],corners[2],u,camera),EdgePoint(corners[0],corners[3],v,camera)};
		Point p=PatchPoint(corners,edges,u,v);
		const float attenuation=ShoreDamping(0.05f*(p.y+20)),base=p.z;
		p.z+=attenuation*Evaluate(p.x,p.y,1.73f).height;
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
