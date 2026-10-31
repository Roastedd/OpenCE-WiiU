/* Frozen de64efde source/physics/collision_bsp.c reference. */
static boolean collision_bsp_test_vector_recursive(
	struct collision_bsp_test_vector_context *data,
	long node_index,
	real t0,
	real t1,
	short depth)
{
	if (!(node_index & LONG_MIN))
	{
		struct bsp3d_node const *node;
		real_plane3d const *plane;
		real distance;
		real dot;
		real distance0;
		real distance1;
		boolean reaches_back;
		boolean reaches_front;

		/* port: (collision_bsp3d_enter_node: the test ends with no more
		hits) */
		if (!collision_bsp3d_enter_node(data->bsp, node_index, depth, &data->nodes_left))
		{
			return FALSE;
		}
		node = TAG_BLOCK_GET_ELEMENT(
			&data->bsp->bsp3d.nodes,
			node_index,
			struct bsp3d_node);
		plane = TAG_BLOCK_GET_ELEMENT(
			&data->bsp->bsp3d.planes,
			node->plane_designator,
			real_plane3d);
		distance = plane3d_distance_to_point(plane, data->point);
		dot = dot_product3d(data->vector, &plane->n);
		distance0 = dot*t0 + distance;
		distance1 = dot*t1 + distance;
		reaches_back = distance0 < 0.f || distance1 < 0.f;
		reaches_front = distance0 >= 0.f || distance1 >= 0.f;

		if (reaches_back && reaches_front)
		{
			boolean front = dot > 0.f;
			real t = -(distance/dot);

			if (collision_bsp_test_vector_recursive(
				data,
				node->children[!front],
				t0,
				t,
				depth + 1))
			{
				return TRUE;
			}
			if (data->result->t <= t)
			{
				return FALSE;
			}
			data->last_plane_index = node->plane_designator;
			if (collision_bsp_test_vector_recursive(
				data,
				node->children[front],
				t,
				t1,
				depth + 1))
			{
				return TRUE;
			}
		}
		else if (collision_bsp_test_vector_recursive(
			data,
			node->children[reaches_front],
			t0,
			t1,
			depth + 1))
		{
			return TRUE;
		}
	}
	else
	{
		long leaf_index = NONE;
		byte contents = _contents_solid;
		boolean test_surface = FALSE;
		long test_leaf_index;

		/* port: a leaf (from the map) that is no leaf is solid
		(collision_bsp_valid_leaf) */
		if (node_index != NONE &&
			collision_bsp_valid_leaf(data->bsp, node_index & LONG_MAX))
		{
			leaf_index = node_index & LONG_MAX;
			contents = TEST_FLAG(
				TAG_BLOCK_GET_ELEMENT(
					&data->bsp->leaves,
					leaf_index,
					struct collision_leaf)->flags,
				_collision_leaf_contains_two_sided_bit) ?
				_contents_semi_empty : _contents_empty;
		}

		if (TEST_FLAG(data->flags, _collision_test_front_facing_surfaces_bit) &&
			(data->last_contents == _contents_empty ||
				data->last_contents == _contents_semi_empty) &&
			contents == _contents_solid)
		{
			test_leaf_index = data->last_leaf_index;
		}
		else if (TEST_FLAG(data->flags, _collision_test_back_facing_surfaces_bit) &&
			data->last_contents == _contents_solid &&
			(contents == _contents_empty || contents == _contents_semi_empty))
		{
			test_leaf_index = leaf_index;
		}
		else if (!TEST_FLAG(data->flags, _collision_test_ignore_two_sided_surfaces_bit) &&
			data->last_contents == _contents_semi_empty &&
			contents == _contents_semi_empty)
		{
			test_leaf_index =
				TEST_FLAG(data->flags, _collision_test_front_facing_surfaces_bit) ?
				data->last_leaf_index : leaf_index;
			test_surface = TRUE;
		}
		else
		{
			test_leaf_index = NONE;
		}

		if (test_leaf_index != NONE)
		{
			long surface_index = collision_leaf_test_vector(
				data->bsp,
				data->breakable_surface_count,
				data->breakable_surface_flags,
				data->point,
				data->vector,
				test_leaf_index,
				data->last_plane_index,
				t0,
				test_surface);

			if (surface_index != NONE)
			{
				struct collision_surface const *surface = TAG_BLOCK_GET_ELEMENT(
					&data->bsp->surfaces,
					surface_index,
					struct collision_surface);

				if ((!TEST_FLAG(surface->flags, _collision_surface_invisible_bit) ||
						!TEST_FLAG(data->flags, _collision_test_ignore_invisible_surfaces_bit)) &&
					(!TEST_FLAG(surface->flags, _collision_surface_breakable_bit) ||
						!TEST_FLAG(data->flags, _collision_test_ignore_breakable_surfaces_bit)))
				{
					data->result->t = t0;
					data->result->plane = TAG_BLOCK_GET_ELEMENT(
						&data->bsp->bsp3d.planes,
						data->last_plane_index,
						real_plane3d);
					data->result->surface_index = surface_index;
					data->result->plane_designator = surface->plane_designator;
					data->result->flags = surface->flags;
					data->result->breakable_surface_index =
						surface->breakable_surface_index;
					data->result->material_index = surface->material_index;

					return TRUE;
				}
			}
		}

		if (leaf_index != NONE)
		{
			if (data->result->leaf_count < 256)
			{
				data->result->leaf_indices[data->result->leaf_count] = leaf_index;
				data->result->leaf_count++;
			}
			else
			{
				data->result->leaf_indices[255] = leaf_index;
			}
		}
		data->last_leaf_index = leaf_index;
		data->last_contents = contents;
	}

	return FALSE;
}
