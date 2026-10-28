# Environment textures

Poly Haven originals, downloaded as 2K PNG diffuse and DirectX tangent-space normal maps.

| Surface | Asset | Authors | License |
|---|---|---|---|
| Wall | [Painted Plaster Wall](https://polyhaven.com/a/painted_plaster_wall) | Amal Kumar | CC0 |
| Floor | [Concrete Floor Worn 001](https://polyhaven.com/a/concrete_floor_worn_001) | Dimitrios Savva, Rico Cilliers | CC0 |
| Ceiling | [Ceiling Interior](https://polyhaven.com/a/ceiling_interior) | Dimitrios Savva | CC0 |

[Poly Haven asset license](https://polyhaven.com/license).

## Application

- `Scenes/Pacman.scene` and `Scenes/MainMenu.scene` reference textured derivatives of the existing map and cubes in `Assets/Models/Map`.
- Original `Map.obj` / `Map.mtl`, actor transforms, collision settings and authored map grids are preserved.
- The original map's `BLACK` section is the floor slab; `map` is the maze walls. The derivatives use separate Floor and Wall materials with new planar UVs. Positions, normals, vertex order and triangle winding are preserved.
- Walls and ceilings tile every 2 world metres; the floor tiles every 3 metres. Each scene has its own UV variant to account for its existing actor and mesh-component scales (Pacman map: 2; MainMenu map: 3). The end-wall cubes in Pacman use the wall material too.
- Scene material overrides use `DefaultLit`, tinted BaseColor, low specular, `LinearWrap`, and `FlipNormalY=false` with the downloaded DX normals. Wall tint compensates for the diffuse texture's sampled mean linear RGB to preserve the original map wall's blue `(0.000448, 0, 0.194330)` on average. Floor and ceiling use a near-black mean diffuse response of `(0.0015, 0.0015, 0.0015)` so surface detail can still respond to light; this is deliberately slightly above the original zero reflectance. The downloaded texture pixels and normal maps are unchanged. No Unlit override, light changes, roughness/displacement maps or renderer changes were added.
- `tools/prepare-environment-materials.cjs` reproduces the OBJ/MTL derivatives and changes only mesh/material fields in these two scenes. It removes a derivative's stale `.obj.bin` when regenerating that derivative or changing its MTL because the existing importer always prefers this cache. The next editor launch cooks it normally. The original map cache is untouched.
- Engine structure changes: none. No build or gameplay tests were run.
