#include "navigation_mesh.h"
#include "navigation_query.h"
#include "navigation.h"



using namespace godot;

void DetourNavigationMesh::_register_methods() {
	register_method("_ready", &DetourNavigationMesh::_ready);
	register_method("_notification", &DetourNavigationMesh::_notification);
	register_method("_exit_tree", &DetourNavigationMesh::_exit_tree);
	register_method("bake_navmesh", &DetourNavigationMesh::build_navmesh);
	register_method("_on_renamed", &DetourNavigationMesh::_on_renamed);
	register_method("clear_navmesh", &DetourNavigationMesh::clear_navmesh);
	register_property<DetourNavigationMesh, Ref<NavmeshParameters>>("parameters", &DetourNavigationMesh::navmesh_parameters, Ref<NavmeshParameters>(), 
		GODOT_METHOD_RPC_MODE_DISABLED, GODOT_PROPERTY_USAGE_DEFAULT, GODOT_PROPERTY_HINT_RESOURCE_TYPE, "Resource");
}

void DetourNavigationMesh::_init() {
}

void DetourNavigationMesh::_exit_tree() {
}


void DetourNavigationMesh::_ready() {
	get_tree()->connect("node_renamed", this, "_on_renamed");
	navmesh_name = get_name().utf8().get_data();
	/*
	Godot::print("Adding obstacle");
	DetourNavigationMeshCached* cached_navm = (DetourNavigationMeshCached *) navmesh;
	unsigned int id = cached_navm->add_obstacle(Vector3(-5.f, 0.f, -5.f), 4.f, 3.f);
	dtTileCache* tile_cache = cached_navm->get_tile_cache();
	tile_cache->update(0.1f, cached_navm->get_detour_navmesh());
	*/
}

DetourNavigationMesh::DetourNavigationMesh(){
}

DetourNavigationMesh::~DetourNavigationMesh() {
	clear_debug_mesh();
	release_navmesh();
	if (navmesh_parameters.is_valid()) {
		navmesh_parameters.unref();
	}
}


void DetourNavigationMesh::_on_renamed(Variant v) {
	char* previous_path = get_cache_file_path();
	navmesh_name = get_name().utf8().get_data();
	char* current_path = get_cache_file_path();
	FileManager::moveFile(previous_path, current_path);
}

char *DetourNavigationMesh::get_cache_file_path() {
	std::string postfix = ".bin";
	std::string prefix = ".navcache/";
	std::string main = get_name().utf8().get_data();
	return _strdup((prefix + main + postfix).c_str());
}


void DetourNavigationMesh::release_navmesh() {
	if (detour_navmesh != nullptr) {
		dtFreeNavMesh(detour_navmesh);
	}
	detour_navmesh = NULL;
	clear_debug_mesh();
}

void DetourNavigationMesh::clear_debug_mesh() {
	if (debug_mesh.is_valid()) {
		debug_mesh.unref();
	}
	if (debug_mesh_instance != nullptr) {
		debug_mesh_instance->queue_free();
		debug_mesh_instance = nullptr;
	}
}

void DetourNavigationMesh::build_navmesh() {
	DetourNavigation* dtmi = Object::cast_to<DetourNavigation>(get_parent());
	if (dtmi == NULL) {
		return;
	}
	clear_navmesh();

	dtmi->build_navmesh(this);
	save_mesh();
}

void DetourNavigationMesh::clear_navmesh() {
	release_navmesh();
}

Ref<ArrayMesh> DetourNavigationMesh::get_debug_mesh() {
	if (debug_mesh.is_valid()) {
		return debug_mesh;
	}
	std::list<Vector3> lines;
	const dtNavMesh* navm = detour_navmesh;
	for (int i = 0; i < navm->getMaxTiles(); i++) {
		const dtMeshTile* tile = navm->getTile(i);
		if (!tile || !tile->header) {
			continue;
		}
		for (int j = 0; j < tile->header->polyCount; j++) {
			dtPoly* poly = tile->polys + j;
			if (poly->getType() != DT_POLYTYPE_OFFMESH_CONNECTION) {
				for (int k = 0; k < poly->vertCount; k++) {
					lines.push_back(
						*reinterpret_cast<const Vector3*>(
							&tile->verts[poly->verts[k] * 3]
						)
					);
					lines.push_back(
						*reinterpret_cast<const Vector3*>(
							&tile->verts[poly->verts[(k + 1) % poly->vertCount] * 3]
						)
					);
				}
			}
			else if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
				const dtOffMeshConnection* con =
					&tile->offMeshCons[j - tile->header->offMeshBase];
				const float* va = &tile->verts[poly->verts[0] * 3];
				const float* vb = &tile->verts[poly->verts[1] * 3];

				Vector3 p0 = *reinterpret_cast<const Vector3*>(va);
				Vector3 p1 = *reinterpret_cast<const Vector3*>(&con[0]);
				Vector3 p2 = *reinterpret_cast<const Vector3*>(&con[3]);
				Vector3 p3 = *reinterpret_cast<const Vector3*>(vb);
				lines.push_back(p0);
				lines.push_back(p1);
				lines.push_back(p1);
				lines.push_back(p2);
				lines.push_back(p2);
				lines.push_back(p3);
			}
		}
	}

	debug_mesh = Ref<ArrayMesh>(ArrayMesh::_new());
	PoolVector3Array varr;
	varr.resize(lines.size());
	PoolVector3Array::Write w = varr.write();
	int idx = 0;
	for (Vector3 vec : lines) {
		w[idx++] = vec;
	}


	Array arr;
	arr.resize(Mesh::ARRAY_MAX);
	arr[Mesh::ARRAY_VERTEX] = varr;

	debug_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, arr);
	return debug_mesh;
}


dtNavMesh* DetourNavigationMesh::load_mesh() {
	dtNavMesh* dt_navmesh = FileManager::loadNavigationMesh(get_cache_file_path());
	if (dt_navmesh == 0) {
		return 0;
	}
	else {
		detour_navmesh = dt_navmesh;
		if (get_tree()->is_debugging_navigation_hint() || Engine::get_singleton()->is_editor_hint()){
			build_debug_mesh();
		}
		return dt_navmesh;
	}
}

void DetourNavigationMesh::save_mesh() {
	FileManager::createDirectory(".navcache");
	FileManager::saveNavigationMesh(get_cache_file_path(), get_detour_navmesh());
}


void DetourNavigationMesh::build_debug_mesh() {
	clear_debug_mesh();
	debug_mesh_instance = MeshInstance::_new();
	debug_mesh_instance->set_mesh(get_debug_mesh());
	debug_mesh_instance->set_material_override(get_debug_navigation_material());
	add_child(debug_mesh_instance);
}


Ref<Material> DetourNavigationMesh::get_debug_navigation_material() {
	/*	Create a navigation mesh material -
		it is not exposed in godot, so we have to create it here again
	*/
	Ref<SpatialMaterial> line_material = Ref<SpatialMaterial>(SpatialMaterial::_new());
	line_material->set_flag(SpatialMaterial::FLAG_UNSHADED, true);
	line_material->set_feature(SpatialMaterial::FEATURE_TRANSPARENT, true);
	line_material->set_flag(SpatialMaterial::FLAG_SRGB_VERTEX_COLOR, true);
	line_material->set_flag(SpatialMaterial::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	line_material->set_albedo(Color(0.1f, 1.0f, 0.7f, 0.4f));

	return line_material;
}

void DetourNavigationMesh::find_path() {
	DetourNavigationQuery* nav_query = new DetourNavigationQuery();
	nav_query->init(this, get_global_transform());
	//Dictionary result = nav_query->find_path(Vector3(0.f, 0.f, 0.f), Vector3(11.f, 0.3f, 11.f), Vector3(50.0f, 3.f, 50.f), new DetourNavigationQueryFilter());
	Dictionary result = nav_query->find_path(Vector3(3.f, 0.f, -15.f), Vector3(-5.6f, 0.3f, 2.8f), Vector3(50.0f, 3.f, 50.f), new DetourNavigationQueryFilter());
	result.clear();
}

void DetourNavigationMesh::_notification(int p_what) {
	switch (p_what) {
	case NOTIFICATION_PREDELETE: {
		/*if (Engine::get_singleton()->is_editor_hint()) {
			FileManager::deleteFile(get_cache_file_path());
		}*/
	} break;
	/*case NOTIFICATION_EXIT_TREE: {
		if (debug_mesh_instance) {
			debug_mesh_instance->free();
			debug_mesh_instance->queue_free();
			debug_mesh_instance = NULL;
		}
		// Tile cache
		set_process(false);
	} break;*/
	}
}