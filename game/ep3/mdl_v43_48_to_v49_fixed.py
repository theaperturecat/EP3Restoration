#!/usr/bin/env python3
"""Convert Source Engine MDL model directories to the Alien Swarm full-v49 MDL/VTX profile.

Conversion paths:
    * Episode One / Source-2004 v44: a dedicated binary parser validates the
      legacy studio header, bones, facial flex/eyeball data, procedural records,
      local animation chains, IK rules, sequences, nested tables, and referenced
      ranges. The writer translates legacy-only slots (including the v49 flex
      vertex-animation type discriminator) while preserving vertex deltas,
      geometry, animation payloads, and offsets.
    * Modern v45-v48: byte-preserving MDL conversion applies Valve's versioned
      animation/cache fixups, then updates the version. Companion VTX v7 files
      are expanded from legacy headers to the full Alien Swarm v49 layout.
    * Modern-layout v44: detected from modern header-tail fields (or forced with
      --modern-v44) and byte-patched using the modern v44-v47 layout. Episode One
      v44 continues through the dedicated legacy parser.
    * v48: a validated byte-preserving patch removes the v48-only boing tail/flag
      while preserving all unrelated source bytes and sidecars.
    * v43: no authoritative public schema was found. An opt-in experimental
      path can test the v44 legacy layout against a v43 file, but it is never
      assumed by default and must pass the same strict parser checks.

--preserve-unclaimed-bytes remains available for the optional ValveMDL path.
The target is the Alien Swarm full v49 profile, not the L4D2 legacy VTX profile.
VVD v4 sidecars are preserved byte-for-byte; VTX v7 sidecars are checksum-
validated and upgraded to the full-v49 strip/group header layout. Real sidecar
checksum mismatches always remain errors.
PHY and ANI files are retained unchanged; referenced external ANI blocks are
checked for presence and their IDAG signature, but the payload is not decoded.
Legacy included-model references are preserved and must also be available in
the game/model search paths for a complete character model.

Install the modern-format fallback with: py -m pip install valvemdl
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import sys
import stat
from dataclasses import dataclass
from pathlib import Path
from typing import Any

TARGET_VERSION = 49
SUPPORTED_SOURCE_VERSIONS = {43, 44, 45, 46, 47, 48}
MDL_MAGIC = b"IDST"
JIGGLE_PROCTYPE = 5  # STUDIO_PROC_JIGGLE
JIGGLE_IS_BOING = 0x80
V48_JIGGLE_SIZE = 140
V49_JIGGLE_SIZE = 120
MDL_HEADER_SIZE = 408
BONE_SIZE = 216
HEADER_SURFACEPROP_OFFSET = 308
HEADER_BONECOUNT_OFFSET = 156
HEADER_BONEINDEX_OFFSET = 160
BONE_PROCTYPE_OFFSET = 164
BONE_PROCINDEX_OFFSET = 168
BONE_SURFACEPROP_OFFSET = 176

# The v49 jiggle record is the v48 record without its five trailing boing floats.
# Keep this list explicit so the writer is not dependent on Construct internals.
JIGGLE_V49_FLOAT_FIELDS = (
    "length",
    "tipMass",
    "yawStiffness",
    "yawDamping",
    "pitchStiffness",
    "pitchDamping",
    "alongStiffness",
    "alongDamping",
    "angleLimit",
    "minYaw",
    "maxYaw",
    "yawFriction",
    "yawBounce",
    "minPitch",
    "maxPitch",
    "pitchFriction",
    "pitchBounce",
    "baseMass",
    "baseStiffness",
    "baseDamping",
    "baseMinLeft",
    "baseMaxLeft",
    "baseLeftFriction",
    "baseMinUp",
    "baseMaxUp",
    "baseUpFriction",
    "baseMinForward",
    "baseMaxForward",
    "baseForwardFriction",
)
BOING_FIELDS = {
    "boingImpactSpeed",
    "boingImpactAngle",
    "boingDampingRate",
    "boingFrequency",
    "boingAmplitude",
}


class ConversionError(Exception):
    """Raised when a model cannot be converted without guessing."""


@dataclass
class ConversionResult:
    path: Path
    status: str
    message: str = ""
    jigglebones: int = 0
    boing_flags_removed: int = 0
    surfaceprop_fields_cleared: int = 0
    external_ani: bool = False



def _i32(data: bytes | bytearray, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ConversionError(f"32-bit field at 0x{offset:X} is outside the MDL file.")
    return struct.unpack_from("<i", data, offset)[0]


def _checked_string(data: bytes | bytearray, offset: int, label: str) -> None:
    if offset == 0:
        return
    if offset < MDL_HEADER_SIZE or offset >= len(data):
        raise ConversionError(f"{label} offset 0x{offset:X} is outside the MDL file.")
    if data.find(b"\0", offset) < 0:
        raise ConversionError(f"{label} at 0x{offset:X} is not NUL-terminated.")


def validate_raw_sidecars(source_mdl: Path, checksum: int) -> None:
    """Validate the stable v49 L4D2 VVD/VTX headers without rewriting payloads."""
    vvd = source_mdl.with_suffix(".vvd")
    if vvd.is_file():
        data = vvd.read_bytes()
        if len(data) < 64 or data[:4] != b"IDSV":
            raise ConversionError(f"{vvd.name} has an invalid/truncated VVD header.")
        version, sidecar_checksum = struct.unpack_from("<ii", data, 4)
        if version != 4:
            raise ConversionError(f"{vvd.name} is VVD version {version}; v49 expects version 4.")
        if sidecar_checksum != checksum:
            raise ConversionError(f"{vvd.name} checksum does not match the MDL.")

    for suffix in (".dx90.vtx", ".dx80.vtx", ".sw.vtx"):
        vtx = source_mdl.with_suffix(suffix)
        if not vtx.is_file():
            continue
        data = vtx.read_bytes()
        if len(data) < 36:
            raise ConversionError(f"{vtx.name} has a truncated VTX header.")
        version = _i32(data, 0)
        sidecar_checksum = _i32(data, 16)
        if version != 7:
            raise ConversionError(f"{vtx.name} is VTX version {version}; v49 expects version 7.")
        if sidecar_checksum != checksum:
            raise ConversionError(f"{vtx.name} checksum does not match the MDL.")


def inspect_v48_binary(data: bytes) -> tuple[list[int], int]:
    """Return jiggle flag offsets and boing-flag count after strict bounds checks.

    No structure is serialized. Every pointer is followed only far enough to
    validate the bone/jiggle records that this conversion changes.
    """
    if len(data) < MDL_HEADER_SIZE:
        raise ConversionError("MDL is shorter than the 408-byte studio header.")
    version, _checksum = struct.unpack_from("<ii", data, 4)
    declared_length = _i32(data, 76)
    if version != 48:
        raise ConversionError(f"Raw v48 patch was requested for MDL version {version}.")
    if declared_length != len(data):
        raise ConversionError(
            f"MDL header length is {declared_length}, but the file is {len(data)} bytes."
        )

    surfaceprop = _i32(data, HEADER_SURFACEPROP_OFFSET)
    _checked_string(data, surfaceprop, "studiohdr_t.surfacepropindex")
    hdr2 = _i32(data, 400)
    if hdr2 and (hdr2 < MDL_HEADER_SIZE or hdr2 + 256 > len(data)):
        raise ConversionError(f"studiohdr2_t range 0x{hdr2:X}-0x{hdr2 + 256:X} is invalid.")

    count = _i32(data, HEADER_BONECOUNT_OFFSET)
    index = _i32(data, HEADER_BONEINDEX_OFFSET)
    if count < 0 or count > 1024:
        raise ConversionError(f"Unreasonable bone count: {count}.")
    if count and (index < MDL_HEADER_SIZE or index + count * BONE_SIZE > len(data)):
        raise ConversionError("Bone array points outside the MDL file or is truncated.")

    jiggle_flags: list[int] = []
    boing_count = 0
    for bone_number in range(count):
        bone_offset = index + bone_number * BONE_SIZE
        parent = _i32(data, bone_offset + 4)
        if parent < -1 or parent >= count:
            raise ConversionError(f"Bone {bone_number} has invalid parent index {parent}.")
        proctype = _i32(data, bone_offset + BONE_PROCTYPE_OFFSET)
        procindex = _i32(data, bone_offset + BONE_PROCINDEX_OFFSET)
        surfaceprop_idx = _i32(data, bone_offset + BONE_SURFACEPROP_OFFSET)
        if surfaceprop_idx < 0:
            raise ConversionError(f"Bone {bone_number} has a negative surfaceprop offset.")
        _checked_string(data, bone_offset + surfaceprop_idx if surfaceprop_idx else 0,
                        f"bone {bone_number} surfaceprop")
        if proctype == 0:
            if procindex != 0:
                raise ConversionError(f"Bone {bone_number} has procindex set but proctype is zero.")
            continue
        if proctype not in (1, 2, 3, 4, JIGGLE_PROCTYPE):
            raise ConversionError(f"Bone {bone_number} has unknown procedural type {proctype}.")
        if procindex <= 0:
            raise ConversionError(f"Bone {bone_number} has an invalid procedural offset.")
        rule_offset = bone_offset + procindex
        record_size = V48_JIGGLE_SIZE if proctype == JIGGLE_PROCTYPE else 4
        if rule_offset < MDL_HEADER_SIZE or rule_offset + record_size > len(data):
            raise ConversionError(f"Bone {bone_number} procedural record is outside the MDL file.")
        if proctype == JIGGLE_PROCTYPE:
            flags = _i32(data, rule_offset)
            jiggle_flags.append(rule_offset)
            if flags & JIGGLE_IS_BOING:
                boing_count += 1
    return jiggle_flags, boing_count


def inspect_modern_v44_v47_binary(
    data: bytes, *, accepted_versions: tuple[int, ...] = (45, 46, 47)
) -> tuple[list[int], int]:
    """Validate a modern-layout v44-v47 MDL for a safe v49 version patch.

    This is only used after v44 has been positively distinguished from the
    Episode One/Source-2004 layout. The modern 408-byte studio header and 216-byte
    bone records are layout-compatible with v49. Pre-v48 jiggle records use the
    120-byte form found in v49. No object graph is rebuilt: every source byte
    except the version word and unsupported boing bit is retained verbatim.
    """
    if len(data) < MDL_HEADER_SIZE or data[:4] != MDL_MAGIC:
        raise ConversionError("MDL is shorter than the 408-byte studio header or has bad magic.")
    version, _checksum = struct.unpack_from("<ii", data, 4)
    if version not in accepted_versions:
        expected = ", ".join(str(v) for v in accepted_versions)
        raise ConversionError(
            f"Raw modern patch expected MDL version(s) {expected}, found {version}."
        )
    declared_length = _i32(data, 76)
    if declared_length != len(data):
        raise ConversionError(
            f"MDL header length is {declared_length}, but the file is {len(data)} bytes."
        )

    # Validate the model-wide surface property and optional secondary header.
    _checked_string(data, _i32(data, HEADER_SURFACEPROP_OFFSET),
                    "studiohdr_t.surfacepropindex")
    hdr2 = _i32(data, 400)
    if hdr2 and (hdr2 < MDL_HEADER_SIZE or hdr2 + 256 > len(data)):
        raise ConversionError(f"studiohdr2_t range 0x{hdr2:X}-0x{hdr2 + 256:X} is invalid.")

    count = _i32(data, HEADER_BONECOUNT_OFFSET)
    index = _i32(data, HEADER_BONEINDEX_OFFSET)
    if count < 0 or count > 1024:
        raise ConversionError(f"Unreasonable bone count: {count}.")
    if count and (index < MDL_HEADER_SIZE or index + count * BONE_SIZE > len(data)):
        raise ConversionError("Bone array points outside the MDL file or is truncated.")

    jiggle_flags: list[int] = []
    boing_count = 0
    for bone_number in range(count):
        bone_offset = index + bone_number * BONE_SIZE
        parent = _i32(data, bone_offset + 4)
        if parent < -1 or parent >= count:
            raise ConversionError(f"Bone {bone_number} has invalid parent index {parent}.")
        if parent >= bone_number:
            raise ConversionError(
                f"Bone {bone_number} has parent {parent}, which does not precede it."
            )

        proctype = _i32(data, bone_offset + BONE_PROCTYPE_OFFSET)
        procindex = _i32(data, bone_offset + BONE_PROCINDEX_OFFSET)
        surfaceprop_idx = _i32(data, bone_offset + BONE_SURFACEPROP_OFFSET)
        if surfaceprop_idx < 0:
            raise ConversionError(f"Bone {bone_number} has a negative surfaceprop offset.")
        if surfaceprop_idx:
            _checked_string(
                data, bone_offset + surfaceprop_idx,
                f"bone {bone_number} surfaceprop",
            )

        if proctype == 0:
            if procindex != 0:
                raise ConversionError(
                    f"Bone {bone_number} has procindex set but proctype is zero."
                )
            continue
        if proctype not in (1, 2, 3, 4, JIGGLE_PROCTYPE):
            raise ConversionError(f"Bone {bone_number} has unknown procedural type {proctype}.")
        if procindex <= 0:
            raise ConversionError(f"Bone {bone_number} has an invalid procedural offset.")

        rule_offset = bone_offset + procindex
        # Non-jiggle procedural records are not rewritten; check at least the
        # leading field. The 120-byte pre-v48 jiggle record is fully bounded.
        record_size = V49_JIGGLE_SIZE if proctype == JIGGLE_PROCTYPE else 4
        if rule_offset < MDL_HEADER_SIZE or rule_offset + record_size > len(data):
            raise ConversionError(
                f"Bone {bone_number} procedural record is outside the MDL file."
            )
        if proctype == JIGGLE_PROCTYPE:
            flags = _i32(data, rule_offset)
            jiggle_flags.append(rule_offset)
            if flags & JIGGLE_IS_BOING:
                boing_count += 1

    return jiggle_flags, boing_count


# Alien Swarm / full Source v49 VTX layout migration.
# VTX stays at file version 7, but MDL v49 reads larger strip-group and strip
# headers: each gets two appended int32 fields (numTopologyIndices, topologyOffset).
# Legacy VTX has no topology buffers, so the new fields are zeroed and every
# relative/absolute pointer affected by insertion is rebased.
from bisect import bisect_right as _bisect_right

@dataclass
class _VtxWalk:
    insertions: list[int]
    relocations: list[tuple[int, int, int, str, bool]]
    leaves: list[tuple[int, int, str]]
    groups: list[int]
    strips: list[int]
    counts: dict[str, int]


def _vtx_array(data: bytes | bytearray, parent: int, relative: int,
               count: int, item_size: int, label: str) -> list[int]:
    if count < 0 or count > 2_000_000:
        raise ConversionError(f"{label} has unreasonable count {count}.")
    if count == 0:
        return []
    start = parent + relative
    end = start + count * item_size
    if relative == 0 or start < 0 or end > len(data):
        raise ConversionError(
            f"{label} range 0x{start:X}-0x{end:X} is outside the VTX file."
        )
    return [start + i * item_size for i in range(count)]


def _vtx_add_relative(walk: _VtxWalk, parent: int, field: int,
                      relative: int, label: str) -> None:
    if relative == 0 or parent + relative < 0:
        raise ConversionError(f"{label} has an invalid relative pointer.")
    walk.relocations.append((parent, field, relative, label, False))


def _vtx_add_absolute(walk: _VtxWalk, parent: int, field: int,
                      absolute: int, label: str) -> None:
    if absolute <= 0 or absolute >= 2**31:
        raise ConversionError(f"{label} has an invalid absolute pointer {absolute}.")
    walk.relocations.append((parent, field, absolute, label, True))


def _walk_vtx_layout(data: bytes | bytearray, *, full_v49: bool) -> _VtxWalk:
    """Walk VTX v7 using either the legacy or full-v49 nested header sizes."""
    if len(data) < 36 or _i32(data, 0) != 7:
        raise ConversionError("VTX must have a complete 36-byte version-7 header.")
    lod_count = _i32(data, 20)
    replacement_rel = _i32(data, 24)
    bodypart_count = _i32(data, 28)
    bodypart_rel = _i32(data, 32)
    if lod_count < 0 or lod_count > 8:
        raise ConversionError(f"Invalid VTX LOD count {lod_count}.")
    walk = _VtxWalk([], [], [], [], [], {
        "bodyparts": 0, "models": 0, "lods": 0, "meshes": 0,
        "groups": 0, "strips": 0, "vertices": 0, "indices": 0,
        "bone_state_changes": 0, "material_replacements": 0,
    })

    # Material replacement list: one 8-byte header per LOD.
    replacement_lists = _vtx_array(data, 0, replacement_rel, lod_count, 8,
                                   "material replacement lists")
    if replacement_lists:
        _vtx_add_relative(walk, 0, 24, replacement_rel,
                           "VTX material replacement lists")
    for replacement_list in replacement_lists:
        count = _i32(data, replacement_list)
        relative = _i32(data, replacement_list + 4)
        records = _vtx_array(data, replacement_list, relative, count, 6,
                             "material replacement records")
        if records:
            _vtx_add_relative(walk, replacement_list, 4, relative,
                              "material replacement array")
        for record in records:
            name_relative = _i32(data, record + 2)
            if name_relative:
                _vtx_add_relative(walk, record, 2, name_relative,
                                  "material replacement name")
                name_offset = record + name_relative
                if name_offset < 0 or name_offset >= len(data) or data.find(b"\0", name_offset) < 0:
                    raise ConversionError(
                        f"Material replacement name at 0x{name_offset:X} is not NUL-terminated."
                    )
            walk.counts["material_replacements"] += 1
            walk.leaves.append((record, 6, "material replacement"))

    bodyparts = _vtx_array(data, 0, bodypart_rel, bodypart_count, 8,
                           "VTX body parts")
    if bodyparts:
        _vtx_add_relative(walk, 0, 32, bodypart_rel, "VTX body parts")
    walk.counts["bodyparts"] = len(bodyparts)

    group_size = 33 if full_v49 else 25
    strip_size = 35 if full_v49 else 27
    for bodypart in bodyparts:
        model_count, model_relative = struct.unpack_from("<ii", data, bodypart)
        models = _vtx_array(data, bodypart, model_relative, model_count, 8,
                            "body-part models")
        if models:
            _vtx_add_relative(walk, bodypart, 4, model_relative, "body-part models")
        walk.counts["models"] += len(models)
        for model in models:
            lods_count, lod_relative = struct.unpack_from("<ii", data, model)
            lods = _vtx_array(data, model, lod_relative, lods_count, 12,
                              "model LODs")
            if lods:
                _vtx_add_relative(walk, model, 4, lod_relative, "model LODs")
            walk.counts["lods"] += len(lods)
            for lod in lods:
                mesh_count, mesh_relative = struct.unpack_from("<ii", data, lod)
                meshes = _vtx_array(data, lod, mesh_relative, mesh_count, 9,
                                    "LOD meshes")
                if meshes:
                    _vtx_add_relative(walk, lod, 4, mesh_relative, "LOD meshes")
                walk.counts["meshes"] += len(meshes)
                for mesh in meshes:
                    group_count, group_relative = struct.unpack_from("<ii", data, mesh)
                    groups = _vtx_array(data, mesh, group_relative, group_count,
                                        group_size, "mesh strip groups")
                    if groups:
                        _vtx_add_relative(walk, mesh, 4, group_relative,
                                          "mesh strip groups")
                    walk.counts["groups"] += len(groups)
                    for group in groups:
                        walk.groups.append(group)
                        (vertex_count, vertex_relative, index_count, index_relative,
                         strip_count, strip_relative) = struct.unpack_from("<iiiiii", data, group)
                        vertices = _vtx_array(data, group, vertex_relative, vertex_count,
                                              9, "strip-group vertices")
                        indices = _vtx_array(data, group, index_relative, index_count,
                                             2, "strip-group indices")
                        strips = _vtx_array(data, group, strip_relative, strip_count,
                                            strip_size, "strip headers")
                        if vertices:
                            _vtx_add_relative(walk, group, 4, vertex_relative,
                                              "strip-group vertices")
                            walk.leaves.append((vertices[0], len(vertices) * 9,
                                                "strip-group vertex buffer"))
                        if indices:
                            _vtx_add_relative(walk, group, 12, index_relative,
                                              "strip-group indices")
                            walk.leaves.append((indices[0], len(indices) * 2,
                                                "strip-group index buffer"))
                        if strips:
                            _vtx_add_relative(walk, group, 20, strip_relative,
                                              "strip headers")
                        walk.counts["vertices"] += len(vertices)
                        walk.counts["indices"] += len(indices)
                        walk.counts["strips"] += len(strips)
                        for strip in strips:
                            walk.strips.append(strip)
                            (strip_index_count, strip_index_start,
                             strip_vertex_count, strip_vertex_start) = struct.unpack_from(
                                "<iiii", data, strip
                            )
                            if strip_index_start < 0 or strip_index_start + strip_index_count > index_count:
                                raise ConversionError(
                                    "A VTX strip references indices outside its strip-group index buffer."
                                )
                            if strip_vertex_start < 0 or strip_vertex_start + strip_vertex_count > vertex_count:
                                raise ConversionError(
                                    "A VTX strip references vertices outside its strip-group vertex buffer."
                                )
                            change_count, change_relative = struct.unpack_from("<ii", data, strip + 19)
                            # optimize.h defines pBoneStateChange() as this strip
                            # header plus boneStateChangeOffset (struct-relative).
                            changes = _vtx_array(data, strip, change_relative, change_count, 8,
                                                 "bone state changes")
                            if changes:
                                _vtx_add_relative(walk, strip, 23, change_relative,
                                                  "bone state changes")
                                walk.leaves.append((changes[0], len(changes) * 8,
                                                    "bone state changes"))
                            walk.counts["bone_state_changes"] += len(changes)
                        if full_v49:
                            topology_count, topology_relative = struct.unpack_from(
                                "<ii", data, group + 25
                            )
                            topology = _vtx_array(data, group, topology_relative,
                                                  topology_count, 2, "strip-group topology indices")
                            if topology:
                                _vtx_add_relative(walk, group, 29, topology_relative,
                                                  "strip-group topology indices")
                                walk.leaves.append((topology[0], len(topology) * 2,
                                                    "strip-group topology indices"))
                            for strip in strips:
                                topology_count, topology_relative = struct.unpack_from(
                                    "<ii", data, strip + 27
                                )
                                topology = _vtx_array(data, strip, topology_relative,
                                                      topology_count, 2, "strip topology indices")
                                if topology:
                                    _vtx_add_relative(walk, strip, 31, topology_relative,
                                                      "strip topology indices")
                                    walk.leaves.append((topology[0], len(topology) * 2,
                                                        "strip topology indices"))
    return walk


def convert_legacy_vtx_to_full_v49(data: bytes, expected_checksum: int | None = None
                                   ) -> tuple[bytes, dict[str, int]]:
    """Upgrade VTX v7 legacy strip records to the Alien Swarm full-v49 layout."""
    if len(data) < 20:
        raise ConversionError("VTX file is truncated.")
    checksum = _i32(data, 16) & 0xFFFFFFFF
    if expected_checksum is not None and checksum != (expected_checksum & 0xFFFFFFFF):
        raise ConversionError("VTX checksum does not match the MDL.")

    old = _walk_vtx_layout(data, full_v49=False)
    insertions = sorted([group + 25 for group in old.groups] +
                        [strip + 27 for strip in old.strips])
    if len(insertions) != len(set(insertions)):
        raise ConversionError("VTX expanded records share an insertion boundary; refusing ambiguous rewrite.")
    for insertion in insertions:
        for start, size, label in old.leaves:
            if start < insertion < start + size:
                raise ConversionError(
                    f"VTX header expansion would split {label} at 0x{insertion:X}."
                )

    upgraded = bytearray(data)
    for insertion in reversed(insertions):
        upgraded[insertion:insertion] = b"\0" * 8

    def relocate(old_offset: int) -> int:
        return old_offset + 8 * _bisect_right(insertions, old_offset)

    for parent, field, value, label, absolute in old.relocations:
        old_target = value if absolute else parent + value
        new_parent = relocate(parent)
        new_target = relocate(old_target)
        new_value = new_target if absolute else new_target - new_parent
        if not -(2**31) <= new_value < 2**31:
            raise ConversionError(f"VTX {label} offset overflows int32.")
        struct.pack_into("<i", upgraded, new_parent + field, new_value)

    new = _walk_vtx_layout(upgraded, full_v49=True)
    for group in new.groups:
        if any(upgraded[group + 25:group + 33]):
            raise ConversionError(f"VTX strip-group topology fields were not initialized at 0x{group:X}.")
    for strip in new.strips:
        if any(upgraded[strip + 27:strip + 35]):
            raise ConversionError(f"VTX strip topology fields were not initialized at 0x{strip:X}.")
    for start, size, label in old.leaves:
        if any(start < insertion < start + size for insertion in insertions):
            raise ConversionError(f"VTX expansion intersects {label}.")
        new_start = relocate(start)
        if upgraded[new_start:new_start + size] != data[start:start + size]:
            raise ConversionError(f"VTX payload verification failed for {label} at 0x{start:X}.")
    for key, before in old.counts.items():
        if new.counts[key] != before:
            raise ConversionError(f"VTX {key} count changed during full-v49 expansion.")
    result_counts = dict(old.counts)
    result_counts["expanded_bytes"] = len(upgraded) - len(data)
    return bytes(upgraded), result_counts


def _prepare_vtx_sidecars(source_mdl: Path, input_root: Path, output_root: Path,
                          checksum: int, *, dry_run: bool, layout: str,
                          source_version: int) -> tuple[list[tuple[Path, Path, bytes]], dict[str, int]]:
    """Validate/convert sibling VTX files to the selected v49 runtime layout."""
    suffixes = (".dx90.vtx", ".dx80.vtx", ".sw.vtx")
    prepared: list[tuple[Path, Path, bytes]] = []
    totals = {"files": 0, "groups": 0, "strips": 0, "expanded_bytes": 0}
    for suffix in suffixes:
        source = source_mdl.with_suffix(suffix)
        if not source.is_file():
            continue
        original = source.read_bytes()
        if len(original) < 36:
            raise ConversionError(f"{source.name} has a truncated VTX header.")
        sidecar_checksum = _i32(original, 16) & 0xFFFFFFFF
        if sidecar_checksum != (checksum & 0xFFFFFFFF):
            raise ConversionError(f"{source.name} checksum does not match the MDL.")
        if _i32(original, 0) != 7:
            raise ConversionError(f"{source.name} is VTX version {_i32(original, 0)}; v49 expects version 7.")
        if layout == "alien-swarm":
            # A v49 MDL could already have full-ASW VTX, or could be a legacy
            # L4D2-style v49 file from an earlier run. Detect before converting.
            already_full = False
            if source_version >= 49:
                try:
                    stats = _walk_vtx_layout(original, full_v49=True).counts
                    already_full = True
                except ConversionError:
                    pass
            if already_full:
                upgraded = original
                for key in ("groups", "strips"):
                    totals[key] += stats[key]
            else:
                try:
                    upgraded, stats = convert_legacy_vtx_to_full_v49(original, checksum)
                except ConversionError as legacy_error:
                    # Some older MDLs may be paired with VTX already authored in
                    # the full-v49 layout. Accept only if the full parser validates.
                    try:
                        stats = _walk_vtx_layout(original, full_v49=True).counts
                        upgraded = original
                    except ConversionError:
                        raise legacy_error
                for key in ("groups", "strips", "expanded_bytes"):
                    totals[key] += stats.get(key, 0)
        elif layout == "legacy":
            upgraded = original
        else:
            raise ConversionError(f"Unknown VTX layout profile: {layout}")
        destination = output_root / source.resolve().relative_to(input_root.resolve())
        prepared.append((source, destination, upgraded))
        totals["files"] += 1
    return prepared, totals


def _write_prepared_sidecars(prepared: list[tuple[Path, Path, bytes]], *, full_v49: bool = True) -> None:
    temps: list[tuple[Path, Path, Path]] = []
    try:
        for source, destination, payload in prepared:
            destination.parent.mkdir(parents=True, exist_ok=True)
            temp = destination.with_name(destination.name + ".v49_tmp")
            temp.write_bytes(payload)
            # Output VTX must still be internally traversable before replacing anything.
            _walk_vtx_layout(payload, full_v49=full_v49)
            temps.append((temp, destination, source))
        for temp, destination, source in temps:
            replace_preserving_metadata(temp, destination, source)
    finally:
        for temp, _destination, _source in temps:
            if temp.exists():
                temp.unlink()


def inspect_modern_v49_migration(data: bytes, version: int) -> tuple[dict[int, int], list[str]]:
    """Apply Valve's documented v45-v48 -> v49 studiohdr compatibility fixups."""
    patches: dict[int, int] = {}
    notes: list[str] = []

    if version < 47:
        old_cache = _i32(data, 380)  # studiohdr_t::unused4 in the v49 header
        if old_cache:
            patches.update({380 + i: 0 for i in range(4)})
            notes.append("cleared pre-v47 unused4 zero-frame cache field")

    anim_count = _i32(data, 180)
    anim_index = _i32(data, 184)
    if anim_count < 0 or anim_count > 65536:
        raise ConversionError(f"Unreasonable local animation count: {anim_count}.")
    if anim_count and (anim_index < MDL_HEADER_SIZE or anim_index + anim_count * 100 > len(data)):
        raise ConversionError("Local animation descriptor array is outside the MDL file.")

    disabled_sectioned = 0
    cleared_zero_frame = 0
    cleared_frameanim = 0
    for i in range(anim_count):
        off = anim_index + i * 100
        flags = _i32(data, off + 12)
        sectionframes = _i32(data, off + 84)
        zero_span = struct.unpack_from("<h", data, off + 88)[0]
        zero_index = _i32(data, off + 92)

        # Valve's v45->v49 fixup disables old sectioned .ANI animations because
        # their index scheme cannot be loaded by the v49 engine.
        if version < 46 and sectionframes != 0:
            for position in range(off + 16, off + 100):
                patches[position] = 0
            patches.update({off + 16: 1, off + 52: 0xFF, off + 53: 0xFF,
                            off + 54: 0xFF, off + 55: 0xFF})  # animblock = -1
            disabled_sectioned += 1
            sectionframes = 0
            zero_span = 0
            zero_index = 0

        if version < 47:
            if zero_span or zero_index:
                patches.update({off + 88: 0, off + 89: 0})
                patches.update({off + 92 + j: 0 for j in range(4)})
                cleared_zero_frame += 1
        elif version == 47 and zero_index != 0:
            patches.update({off + 88: 0, off + 89: 0})
            patches.update({off + 92 + j: 0 for j in range(4)})
            cleared_zero_frame += 1

        # STUDIO_FRAMEANIM (0x0040) was stale on all pre-v49 versions.
        if flags & 0x0040:
            new_flags = flags & ~0x0040
            encoded = struct.pack("<i", new_flags)
            patches.update({off + 12 + j: value for j, value in enumerate(encoded)})
            cleared_frameanim += 1

    if disabled_sectioned:
        notes.append(f"disabled {disabled_sectioned} incompatible v45 sectioned animation(s)")
    if cleared_zero_frame:
        notes.append(f"cleared stale zero-frame cache on {cleared_zero_frame} animation(s)")
    if cleared_frameanim:
        notes.append(f"cleared stale STUDIO_FRAMEANIM flag on {cleared_frameanim} animation(s)")
    return patches, notes


def write_byte_preserving_v49(source_mdl: Path, output_root: Path, input_root: Path,
                              version: int, dry_run: bool, raw_jiggle_offsets: list[int],
                              boing_count: int, external_ani: bool, *,
                              compatibility_patches: dict[int, int] | None = None,
                              compatibility_notes: list[str] | None = None,
                              vtx_layout: str = "alien-swarm") -> ConversionResult:
    """Upgrade one MDL and its VTX companions to the Alien Swarm v49 profile."""
    rel = source_mdl.relative_to(input_root)
    original = source_mdl.read_bytes()
    _raw_version, checksum, declared_length = read_mdl_header(source_mdl)
    if declared_length != len(original):
        raise ConversionError("MDL declared length does not match its actual file size.")
    validate_raw_sidecars(source_mdl, checksum)
    prepared_vtx, vtx_stats = _prepare_vtx_sidecars(
        source_mdl, input_root, output_root, checksum,
        dry_run=dry_run, layout=vtx_layout, source_version=version,
    )
    compatibility_patches = compatibility_patches or {}
    compatibility_notes = compatibility_notes or []

    if dry_run:
        msg = (f"would convert v{version} -> v49; {len(raw_jiggle_offsets)} jigglebone(s), "
               f"{boing_count} boing flag(s) removed; {len(compatibility_patches)} "
               f"version-compatibility byte(s) patched")
        if vtx_layout == "alien-swarm" and vtx_stats["files"]:
            msg += (f"; upgraded {vtx_stats['files']} VTX file(s) to full Alien Swarm v49 "
                    f"layout ({vtx_stats['groups']} strip groups, {vtx_stats['strips']} strips, "
                    f"+{vtx_stats['expanded_bytes']} bytes)")
        elif vtx_layout == "legacy" and vtx_stats["files"]:
            msg += f"; retained {vtx_stats['files']} legacy VTX file(s) by request"
        if compatibility_notes:
            msg += "; " + "; ".join(compatibility_notes)
        if external_ani:
            msg += "; external .ani will be copied unchanged (not parsed)"
        return ConversionResult(rel, "dry-run", msg, len(raw_jiggle_offsets),
                                boing_count, 0, external_ani)

    patched = bytearray(original)
    struct.pack_into("<i", patched, 4, TARGET_VERSION)
    for offset in raw_jiggle_offsets:
        flags = _i32(patched, offset)
        struct.pack_into("<i", patched, offset, flags & ~JIGGLE_IS_BOING)
    for offset, value in compatibility_patches.items():
        if not 0 <= offset < len(patched) or not 0 <= value <= 0xFF:
            raise ConversionError("Invalid version-compatibility patch generated by migration logic.")
        patched[offset] = value

    # Only the version word, the validated jiggle flags and official version
    # migration fields are permitted to differ in the MDL.
    allowed = set(range(4, 8))
    for offset in raw_jiggle_offsets:
        allowed.update(range(offset, offset + 4))
    allowed.update(compatibility_patches)
    changed = [i for i, (a, b) in enumerate(zip(original, patched)) if a != b]
    unexpected = [i for i in changed if i not in allowed]
    if unexpected:
        raise ConversionError(f"Internal patch attempted to alter unexpected MDL byte 0x{unexpected[0]:X}.")

    output_mdl = output_root / rel
    output_mdl.parent.mkdir(parents=True, exist_ok=True)
    temp_mdl = output_mdl.with_name(output_mdl.stem + ".v49_tmp.mdl")
    try:
        temp_mdl.write_bytes(patched)
        out_version, out_checksum, out_length = read_mdl_header(temp_mdl)
        if (out_version, out_checksum, out_length) != (TARGET_VERSION, checksum, len(original)):
            raise ConversionError("Patched MDL header failed post-write verification.")
        if temp_mdl.read_bytes() != bytes(patched):
            raise ConversionError("Patched MDL changed during disk write verification.")
        # Validate every VTX before replacing the MDL or any companion.
        for _source, _destination, payload in prepared_vtx:
            _walk_vtx_layout(payload, full_v49=(vtx_layout == "alien-swarm"))
        replace_preserving_metadata(temp_mdl, output_mdl, source_mdl)
        _write_prepared_sidecars(prepared_vtx, full_v49=(vtx_layout == "alien-swarm"))
    finally:
        if temp_mdl.exists():
            temp_mdl.unlink()

    msg = (f"converted v{version} -> Alien Swarm v49; {len(raw_jiggle_offsets)} jigglebone(s), "
           f"{boing_count} boing flag(s) removed; {len(compatibility_patches)} "
           f"version-compatibility byte(s) patched")
    if vtx_layout == "alien-swarm" and vtx_stats["files"]:
        msg += (f"; upgraded {vtx_stats['files']} VTX file(s) to full v49 layout "
                f"({vtx_stats['groups']} strip groups, {vtx_stats['strips']} strips, "
                f"+{vtx_stats['expanded_bytes']} bytes)")
    elif vtx_layout == "legacy" and vtx_stats["files"]:
        msg += f"; preserved {vtx_stats['files']} legacy VTX file(s)"
    if compatibility_notes:
        msg += "; " + "; ".join(compatibility_notes)
    if external_ani:
        msg += "; WARNING: external .ani copied unchanged, not decoded"
    return ConversionResult(rel, "converted", msg, len(raw_jiggle_offsets),
                            boing_count, 0, external_ani)

def import_valvemdl():
    """Import ValveMDL lazily so --help works without the dependency installed."""
    try:
        from construct import Float32l, Int32sl, Struct
        from valvemdl import Mdl
        import valvemdl.loader as loader_module
        import valvemdl.unloader as unloader_module
        from valvemdl.structs.bone import mstudiojigglebone_t as v48_jiggle_schema
    except ImportError as exc:
        raise ConversionError(
            "ValveMDL is not installed. Install it with: python -m pip install valvemdl"
        ) from exc

    if not hasattr(loader_module, "PROCEDURAL_BONES"):
        raise ConversionError("This ValveMDL version has no loader.PROCEDURAL_BONES map.")
    if not hasattr(unloader_module, "PROCEDURAL_STRUCTS"):
        raise ConversionError("This ValveMDL version has no unloader.PROCEDURAL_STRUCTS map.")

    # Verify that the source schema really is the expected v48 layout.
    try:
        source_size = v48_jiggle_schema.sizeof()
    except Exception as exc:
        raise ConversionError(f"Could not measure ValveMDL's jigglebone schema: {exc}") from exc
    if source_size != V48_JIGGLE_SIZE:
        raise ConversionError(
            f"Unexpected ValveMDL jigglebone size: {source_size}, expected {V48_JIGGLE_SIZE}."
        )

    v49_jiggle_schema = Struct(
        "flags" / Int32sl,
        *(field / Float32l for field in JIGGLE_V49_FLOAT_FIELDS),
    )
    if v49_jiggle_schema.sizeof() != V49_JIGGLE_SIZE:
        raise ConversionError(
            f"Internal v49 jiggle schema is {v49_jiggle_schema.sizeof()} bytes, "
            f"expected {V49_JIGGLE_SIZE}."
        )

    return Mdl, loader_module, unloader_module, v48_jiggle_schema, v49_jiggle_schema


def read_mdl_header(path: Path) -> tuple[int, int, int]:
    """Return (version, checksum, declared length) from the 408-byte studiohdr_t.

    The name[64] field sits between checksum and length:
      id[4], version[4], checksum[4], name[64], length[4]

    Therefore length is at byte offset 76, not offset 12. Reading it from
    offset 12 interprets the first four letters of the model name as an int
    (e.g. 'airb' -> 1651665249).
    """
    with path.open("rb") as stream:
        data = stream.read(80)
    if len(data) < 80:
        raise ConversionError("file is too short to contain the start of studiohdr_t")
    magic = data[0:4]
    if magic != MDL_MAGIC:
        raise ConversionError(f"bad MDL magic {magic!r}; expected {MDL_MAGIC!r}")
    version, checksum = struct.unpack_from("<ii", data, 4)
    (length,) = struct.unpack_from("<i", data, 76)
    return version, checksum, length


def parsed_header_checksum(mdl: Any) -> int | None:
    """Read the parsed header checksum without invoking version-specific properties.

    Some ValveMDL releases implement ``Mdl.checksum`` as a property that raises
    KeyError when an older header container omits the key. The raw header was
    already read independently, so a missing parsed value is not fatal.
    """
    try:
        header = mdl.header
        if hasattr(header, "get"):
            value = header.get("checksum")
        else:
            value = getattr(header, "checksum", None)
        return None if value is None else int(value)
    except (AttributeError, KeyError, TypeError, ValueError):
        return None


def require_clean_validation(label: str, problems: list[str]) -> None:
    if problems:
        details = "\n      ".join(str(item) for item in problems[:12])
        more = "" if len(problems) <= 12 else f"\n      ... and {len(problems) - 12} more"
        raise ConversionError(f"{label} validation failed:\n      {details}{more}")


def _roundtrip_audit(obj: Any) -> tuple[dict[str, Any], list[int], list[int], int, list[str]]:
    """Return coverage labels for every round-trip mismatch.

    ValveMDL's Rebuild object labels bytes as STRUCT (1), OPAQUE (2), or
    UNCLAIMED (0). Only an unclaimed byte can be preserved from the source as an
    opaque gap; a difference in a claimed byte means a parsed field or measured
    payload was changed and must remain a hard error.
    """
    from valvemdl.unloader import rebuild

    rebuilt_obj = rebuild(obj)
    original = bytes(obj._data)
    rebuilt = bytes(rebuilt_obj.data)
    try:
        labels = bytes(rebuilt_obj.coverage)
    except (AttributeError, TypeError):
        # Older ValveMDL builds may not expose the per-byte map. In that case
        # retain strict behavior: no difference is classified as preservable.
        labels = bytes([1]) * len(rebuilt)
    report = rebuilt_obj.report()
    common = min(len(original), len(rebuilt))
    changed = [i for i in range(common) if original[i] != rebuilt[i]]
    unclaimed = [i for i in changed if i < len(labels) and labels[i] == 0]
    claimed = [i for i in changed if i >= len(labels) or labels[i] != 0]
    length_delta = abs(len(original) - len(rebuilt))
    report["differ"] = len(changed)
    report["unclaimed_differ"] = len(unclaimed)
    report["claimed_differ"] = len(claimed)
    examples = []
    unclaimed_set = set(unclaimed)
    for i in changed[:10]:
        kind = "unclaimed" if i in unclaimed_set else "claimed"
        examples.append(
            f"0x{i:08X}: {original[i]:02X}->{rebuilt[i]:02X} ({kind})"
        )
    if len(original) != len(rebuilt):
        examples.append(f"size {len(original)}->{len(rebuilt)}")
    return report, unclaimed, claimed, length_delta, examples


def roundtrip_diagnostics(obj: Any) -> tuple[dict[str, Any], int, int, list[str]]:
    """Backwards-compatible summary of ValveMDL round-trip diagnostics."""
    report, unclaimed, claimed, length_delta, examples = _roundtrip_audit(obj)
    return report, len(unclaimed) + len(claimed), length_delta, examples


def require_lossless_roundtrip(
    label: str,
    obj: Any,
    *,
    preserve_unclaimed: bool = False,
    max_unclaimed_differences: int = 16,
) -> dict[str, Any]:
    report, unclaimed, claimed, length_delta, examples = _roundtrip_audit(obj)
    changed_count = len(unclaimed) + len(claimed)
    if changed_count or length_delta:
        safe_unclaimed_only = (
            preserve_unclaimed
            and not claimed
            and not length_delta
            and len(unclaimed) <= max_unclaimed_differences
            and int(report.get("collisions", 0)) == 0
        )
        if safe_unclaimed_only:
            # These offsets are gaps in ValveMDL's schema, not fields it parsed.
            # We do not discard or silently ignore them: the writer restores the
            # exact original source bytes at these offsets before output is used.
            report["preserved_unclaimed_bytes"] = len(unclaimed)
            report["_preserve_unclaimed_offsets"] = unclaimed
            return report

        detail = (
            f"{changed_count} differing bytes (unclaimed={len(unclaimed)}, "
            f"claimed={len(claimed)}, coverage reports {report.get('differ', -1)}), "
            f"size delta={length_delta}; coverage={report}"
        )
        if examples:
            detail += "; examples=" + ", ".join(examples)
        if preserve_unclaimed and claimed:
            detail += "; relaxed mode only permits UNCLAIMED bytes, never STRUCT/OPAQUE bytes"
        elif preserve_unclaimed and len(unclaimed) > max_unclaimed_differences:
            detail += f"; unclaimed difference limit is {max_unclaimed_differences}"
        raise ConversionError(
            f"{label} is not safely rebuildable by this ValveMDL version "
            f"({detail}). The exporter would risk dropping or changing data."
        )
    return report


def restore_unclaimed_bytes(destination: Path, source_data: bytes, offsets: list[int]) -> int:
    """Restore specific UNCLAIMED positions from the original MDL byte stream."""
    if not offsets:
        return 0
    original = bytes(source_data)
    current = bytearray(destination.read_bytes())
    if len(current) != len(original):
        raise ConversionError(
            "Cannot preserve unclaimed MDL bytes because the rebuilt file size changed "
            f"({len(original)} -> {len(current)})."
        )
    for offset in offsets:
        if offset < 0 or offset >= len(current):
            raise ConversionError(f"Unclaimed-byte offset 0x{offset:X} is outside the MDL.")
        current[offset] = original[offset]
    destination.write_bytes(current)
    if destination.read_bytes() != bytes(current):
        raise ConversionError("Could not verify restored unclaimed MDL bytes after writing.")
    return len(offsets)


def ensure_parsed_header_checksum(mdl: Any, raw_checksum: int) -> None:
    """Populate ValveMDL's parsed header checksum from the authoritative raw header.

    Some older ValveMDL/Construct combinations produce a header container that
    lacks the checksum key even though the on-disk studiohdr_t always stores it at
    byte offset 8. Re-inserting that exact value fixes both Mdl.checksum and the
    serializer's required field. A present-but-different checksum is not ignored.
    """
    header = getattr(mdl, "header", None)
    if header is None:
        raise ConversionError("ValveMDL did not expose a parsed studiohdr_t header.")
    try:
        present = "checksum" in header
        value = header["checksum"] if present else None
    except (TypeError, AttributeError, KeyError):
        try:
            value = getattr(header, "checksum")
            present = True
        except (AttributeError, KeyError):
            value = None
            present = False
    if present and value is not None and int(value) != int(raw_checksum):
        raise ConversionError(
            f"Parsed MDL checksum {int(value):#010x} does not match raw header "
            f"checksum {int(raw_checksum):#010x}; refusing to relink sidecars."
        )
    if not present or value is None:
        try:
            header["checksum"] = int(raw_checksum)
        except (TypeError, AttributeError):
            try:
                setattr(header, "checksum", int(raw_checksum))
            except Exception as exc:
                raise ConversionError(
                    "ValveMDL omitted studiohdr_t.checksum and its parsed header "
                    "cannot be repaired safely."
                ) from exc


def validate_valvemdl_model(mdl: Any, source_mdl: Path, raw_checksum: int) -> list[str]:
    """Validate structure and sidecars using the raw on-disk checksum as truth."""
    validate_raw_sidecars(source_mdl, raw_checksum)
    ensure_parsed_header_checksum(mdl, raw_checksum)
    try:
        return list(mdl.validate())
    except KeyError as exc:
        if exc.args and exc.args[0] == "checksum":
            # A second path may have cached a schema/header Container without the
            # expected member. Reassert the raw header value, then retry once.
            ensure_parsed_header_checksum(mdl, raw_checksum)
            try:
                return list(mdl.validate())
            except KeyError as retry_exc:
                raise ConversionError(
                    "ValveMDL still raised KeyError('checksum') after the raw "
                    "header checksum was restored; the error is not safely bypassed."
                ) from retry_exc
        raise


def has_modern_v44_header_tail(data: bytes) -> bool:
    """Return True when the legacy tail contains fields reserved by v44+."""
    return len(data) >= MDL_HEADER_SIZE and any(data[378:408])


def is_modern_v44_layout_error(exc: BaseException) -> bool:
    """Identify explicit legacy-parser diagnostics that indicate modern v44 data.

    This is a narrowly-scoped fallback, not a blanket retry: malformed legacy
    files and unrelated parse errors remain failures instead of being guessed.
    """
    message = str(exc).lower()
    return "--modern-v44" in message or "modern v44" in message


def find_jigglebones(mdl: Any) -> list[tuple[Any, Any]]:
    """Return (bone, procedural-rule) pairs for parsed jiggle bones."""
    found = []
    for bone in mdl.bones:
        if int(getattr(bone, "proctype", 0) or 0) != JIGGLE_PROCTYPE:
            continue
        rule = getattr(bone, "procedural", None)
        if rule is None:
            raise ConversionError(
                f"Bone {getattr(bone, 'name', '<unnamed>')!r} is a jigglebone, "
                "but ValveMDL did not load its procedural rule."
            )
        offset = getattr(rule, "_offset", None)
        if not isinstance(offset, int) or offset < 0:
            raise ConversionError("A parsed jigglebone has no valid source-file offset.")
        found.append((bone, rule))
    return found


def replace_preserving_metadata(temp_path: Path, destination: Path, source: Path) -> None:
    """Atomically replace a copied output file, including read-only Windows files."""
    if destination.exists():
        try:
            destination.chmod(destination.stat().st_mode | stat.S_IWRITE)
        except OSError:
            pass
    try:
        os.replace(temp_path, destination)
    except OSError:
        # Windows can refuse replacement of a read-only destination even after
        # chmod (e.g. inherited ACLs); use a remove/replace fallback there.
        if destination.exists():
            destination.unlink()
        os.replace(temp_path, destination)
    shutil.copystat(source, destination)


def export_sidecars(mdl: Any, source_mdl: Path, output_root: Path,
                    input_root: Path, dry_run: bool) -> None:
    """Parse and validate VVD/VTX companions, but preserve their source bytes.

    The Alien Swarm target uses VVD 4 and VTX 7 sidecars, but VTX v7 has two
    record layouts. Full Alien Swarm v49 appends topology fields to strip-group
    and strip records. The dedicated raw migration path handles those records;
    this ValveMDL fallback validates only sidecars it can safely parse.
    """
    for extension, sidecar_name in sorted(mdl.sidecar_paths.items()):
        source_sidecar = Path(sidecar_name)
        try:
            sidecar = mdl.vvd if extension == ".vvd" else mdl.get_vtx(extension)
        except Exception as exc:
            raise ConversionError(f"Could not import sidecar {source_sidecar.name}: {exc}") from exc

        require_clean_validation(source_sidecar.name, sidecar.validate())
        require_lossless_roundtrip(source_sidecar.name, sidecar)

        # output_root is populated with copytree() before models are processed.
        # In dry-run mode there is intentionally nothing to write.
        if not dry_run:
            relative = source_sidecar.resolve().relative_to(input_root.resolve())
            output_sidecar = output_root / relative
            if not output_sidecar.is_file():
                raise ConversionError(
                    f"Expected copied sidecar is missing from output: {output_sidecar}"
                )


def convert_one(source_mdl: Path, input_root: Path, output_root: Path,
                api: tuple[Any, ...], dry_run: bool,
                reject_external_ani: bool, *, modern_v44: bool = False,
                assume_v43_episode_one: bool = False,
                preserve_unclaimed_bytes: bool = False,
                max_unclaimed_differences: int = 16,
                vtx_layout: str = "alien-swarm") -> ConversionResult:
    rel = source_mdl.relative_to(input_root)

    version, raw_checksum, raw_length = read_mdl_header(source_mdl)
    if version == TARGET_VERSION:
        if vtx_layout == "alien-swarm":
            prepared_vtx, vtx_stats = _prepare_vtx_sidecars(
                source_mdl, input_root, output_root, raw_checksum,
                dry_run=dry_run, layout=vtx_layout, source_version=version,
            )
            if vtx_stats["expanded_bytes"]:
                if not dry_run:
                    _write_prepared_sidecars(prepared_vtx, full_v49=True)
                message = (f"upgraded existing v49 model's legacy VTX sidecars to full Alien Swarm layout; "
                           f"{vtx_stats['groups']} strip groups, {vtx_stats['strips']} strips, "
                           f"+{vtx_stats['expanded_bytes']} bytes")
                return ConversionResult(rel, "dry-run" if dry_run else "converted", message)
        return ConversionResult(rel, "already-v49")
    if version not in SUPPORTED_SOURCE_VERSIONS:
        raise ConversionError(
            f"MDL version {version} is not supported by this converter (expected 43-48 or 49)."
        )

    external_ani = source_mdl.with_suffix(".ani").exists()
    if external_ani and reject_external_ani:
        raise ConversionError(
            f"External animation file {source_mdl.with_suffix('.ani').name} exists, but .ani payloads "
            "are preserved rather than parsed. Refusing this model because --reject-external-ani was set."
        )

    # Modern v45-v47 share the v49 120-byte jiggle record. A raw, validated
    # patch is preferable to object-graph rebuilding: it preserves every
    # original payload byte and avoids ValveMDL checksum/round-trip failures.
    if version in (45, 46, 47):
        source_bytes = source_mdl.read_bytes()
        jiggle_offsets, boing_count = inspect_modern_v44_v47_binary(source_bytes)
        migration_patches, migration_notes = inspect_modern_v49_migration(source_bytes, version)
        return write_byte_preserving_v49(
            source_mdl, output_root, input_root, version, dry_run,
            jiggle_offsets, boing_count, external_ani,
            compatibility_patches=migration_patches,
            compatibility_notes=migration_notes,
            vtx_layout=vtx_layout,
        )

    # Modern-format v44 uses the same later Source layout as v45-v47. It is
    # distinguishable from Episode One v44 by modern studiohdr tail fields, or
    # by an explicit --modern-v44 request. Do not send this through ValveMDL:
    # patch the known-compatible header/records in place instead.
    if version == 44:
        source_bytes = source_mdl.read_bytes()
        use_modern_v44 = modern_v44 or has_modern_v44_header_tail(source_bytes)
        if not use_modern_v44:
            try:
                return write_episode_one_v44_as_v49(
                    source_mdl, input_root, output_root, 44, dry_run,
                    vtx_layout=vtx_layout,
                )
            except ConversionError as exc:
                if not is_modern_v44_layout_error(exc):
                    raise
                use_modern_v44 = True
        if use_modern_v44:
            jiggle_offsets, boing_count = inspect_modern_v44_v47_binary(
                source_bytes, accepted_versions=(44,)
            )
            migration_patches, migration_notes = inspect_modern_v49_migration(source_bytes, version)
            return write_byte_preserving_v49(
                source_mdl, output_root, input_root, version, dry_run,
                jiggle_offsets, boing_count, external_ani,
                compatibility_patches=migration_patches,
                compatibility_notes=migration_notes,
                vtx_layout=vtx_layout,
            )

    # v48 -> v49 needs only two well-understood changes. Avoid rebuilding the
    # object graph: ValveMDL deliberately leaves some compressed animation data
    # opaque, and rebuilding would zero those original bytes.
    if version == 48:
        source_bytes = source_mdl.read_bytes()
        jiggle_offsets, boing_count = inspect_v48_binary(source_bytes)
        migration_patches, migration_notes = inspect_modern_v49_migration(source_bytes, version)
        return write_byte_preserving_v49(
            source_mdl, output_root, input_root, version, dry_run,
            jiggle_offsets, boing_count, external_ani,
            compatibility_patches=migration_patches,
            compatibility_notes=migration_notes,
            vtx_layout=vtx_layout,
        )

    # No authoritative v43 schema was located in the public SDK/source
    # references. Permit a deliberate experimental check against the known
    # Episode One layout, but never guess it silently.
    if version == 43:
        if not assume_v43_episode_one:
            raise ConversionError(
                "MDL v43 has no verified schema in the available Source SDK references. "
                "It was not modified. Use --assume-v43-episode-one only when you have "
                "evidence this file uses the Episode One v44 binary layout."
            )
        return write_episode_one_v44_as_v49(
            source_mdl, input_root, output_root, 43, dry_run,
            experimental_v43=True, vtx_layout=vtx_layout,
        )

    if api is None:
        raise ConversionError(
            "This model needs the ValveMDL fallback (modern v45-v47 layout), but ValveMDL "
            "is not installed. Install it with: py -m pip install valvemdl"
        )
    Mdl, loader_module, unloader_module, _v48_jiggle_schema, v49_jiggle_schema = api
    try:
        mdl = Mdl(str(source_mdl))
    except Exception as exc:
        raise ConversionError(f"ValveMDL could not import the model: {exc}") from exc

    if mdl.version != version:
        raise ConversionError(
            f"Header version changed while parsing (raw={version}, ValveMDL={mdl.version})."
        )
    ensure_parsed_header_checksum(mdl, raw_checksum)

    # This branch handles modern-layout v44 fallback and modern v45-v47 inputs.
    # The v44 auto-fallback is triggered by explicit repurposed header/descriptor
    # fields; the import gate below still requires semantic and byte-level checks.

    # Semantic validation and the byte-preservation audit are our import gate.
    # ensure_parsed_header_checksum() restores the standard studiohdr_t checksum
    # member from byte offset 8 before normal sidecar validation is performed.
    problems = validate_valvemdl_model(mdl, source_mdl, raw_checksum)
    require_clean_validation(source_mdl.name, problems)
    mdl_coverage = require_lossless_roundtrip(
        source_mdl.name, mdl,
        preserve_unclaimed=preserve_unclaimed_bytes,
        max_unclaimed_differences=max_unclaimed_differences,
    )

    # Confirm the bytes we read at the top-level header are internally consistent.
    source_bytes = source_mdl.read_bytes()
    if raw_length != len(source_bytes):
        raise ConversionError(
            f"MDL header length is {raw_length}, but the file is {len(source_bytes)} bytes."
        )
    parsed_checksum = parsed_header_checksum(mdl)
    if parsed_checksum is not None and parsed_checksum != raw_checksum:
        raise ConversionError("MDL checksum changed between raw read and ValveMDL import.")

    jiggles = find_jigglebones(mdl)

    # Parse and round-trip the VVD/VTX companions. Their raw headers/checksums and
    # model-to-VVD vertex totals have already been checked separately. They stay identical.
    export_sidecars(mdl, source_mdl, output_root, input_root, dry_run)

    boing_flags_removed = 0
    for _bone, rule in jiggles:
        flags = int(rule.flags)
        if flags & JIGGLE_IS_BOING:
            boing_flags_removed += 1
            rule.flags = flags & ~JIGGLE_IS_BOING

    # surfacepropindex/surfacepropidx are real offsets in the supported Source
    # layouts, including v48. Preserve them; clearing them destroys physics data.
    surfaceprop_fields_cleared = 0
    mdl.header.version = TARGET_VERSION

    if dry_run:
        message = (
            f"would convert v{version} -> v49; {len(jiggles)} jigglebone(s), "
            f"{boing_flags_removed} boing flag(s) removed, "
            f"{surfaceprop_fields_cleared} reserved surfaceprop slot(s) cleared"
        )
        preserved_unclaimed = len(mdl_coverage.get("_preserve_unclaimed_offsets", []))
        if preserved_unclaimed:
            message += f"; would preserve {preserved_unclaimed} unclaimed byte(s) verbatim"
        if external_ani:
            message += "; external .ani will be copied unchanged (not parsed)"
        return ConversionResult(
            rel, "dry-run", message, len(jiggles), boing_flags_removed,
            surfaceprop_fields_cleared, external_ani
        )

    output_mdl = output_root / rel
    output_mdl.parent.mkdir(parents=True, exist_ok=True)
    temp_mdl = output_mdl.with_name(output_mdl.stem + ".v49_tmp.mdl")

    # Patch both maps used by ValveMDL's importer and exporter. The original mapping
    # objects are restored in finally, so this does not leak into later conversions.
    loader_map = loader_module.PROCEDURAL_BONES
    unloader_map = unloader_module.PROCEDURAL_STRUCTS
    old_loader_schema = loader_map.get(JIGGLE_PROCTYPE)
    old_unloader_schema = unloader_map.get(JIGGLE_PROCTYPE)
    try:
        loader_map[JIGGLE_PROCTYPE] = v49_jiggle_schema
        unloader_map[JIGGLE_PROCTYPE] = v49_jiggle_schema

        mdl.save(str(temp_mdl))
        restore_unclaimed_bytes(
            temp_mdl, source_bytes,
            list(mdl_coverage.get("_preserve_unclaimed_offsets", [])),
        )

        # Re-import with the target schema and verify it can be rebuilt safely.
        converted = Mdl(str(temp_mdl))
        ensure_parsed_header_checksum(converted, raw_checksum)
        if converted.version != TARGET_VERSION:
            raise ConversionError(
                f"Exporter wrote version {converted.version}, not {TARGET_VERSION}."
            )
        converted_coverage = require_lossless_roundtrip(
            f"converted {source_mdl.name}", converted,
            preserve_unclaimed=preserve_unclaimed_bytes,
            max_unclaimed_differences=max_unclaimed_differences,
        )
        target_unclaimed_offsets = list(
            converted_coverage.get("_preserve_unclaimed_offsets", [])
        )
        restore_unclaimed_bytes(
            temp_mdl, source_bytes, target_unclaimed_offsets
        )
        target_problems = [
            p for p in converted.validate()
            if not str(p).startswith("unknown version 49")
        ]
        require_clean_validation(f"converted {source_mdl.name}", target_problems)

        # The MDL checksum must remain identical so the untouched .phy/.ani and
        # round-tripped VVD/VTX files remain linked to this model set.
        converted_checksum = parsed_header_checksum(converted)
        if converted_checksum is not None and converted_checksum != raw_checksum:
            raise ConversionError("MDL checksum changed during conversion.")
        if converted.header.length != temp_mdl.stat().st_size:
            raise ConversionError("Converted MDL header length does not match output size.")

        # Validate the serialized header directly as well as through ValveMDL.
        out_version, out_checksum, out_length = read_mdl_header(temp_mdl)
        if (out_version, out_checksum, out_length) != (
            TARGET_VERSION, raw_checksum, temp_mdl.stat().st_size
        ):
            raise ConversionError("Converted MDL's raw header failed verification.")

        replace_preserving_metadata(temp_mdl, output_mdl, source_mdl)
    finally:
        if old_loader_schema is None:
            loader_map.pop(JIGGLE_PROCTYPE, None)
        else:
            loader_map[JIGGLE_PROCTYPE] = old_loader_schema
        if old_unloader_schema is None:
            unloader_map.pop(JIGGLE_PROCTYPE, None)
        else:
            unloader_map[JIGGLE_PROCTYPE] = old_unloader_schema
        if temp_mdl.exists():
            temp_mdl.unlink()

    message = (
        f"converted v{version} -> v49; {len(jiggles)} jigglebone(s), "
        f"{boing_flags_removed} boing flag(s) removed, "
        f"{surfaceprop_fields_cleared} reserved surfaceprop slot(s) cleared; "
        f"source round-trip diff={mdl_coverage.get('differ')}, "
        f"target round-trip diff={converted_coverage.get('differ')}"
    )
    preserved_total = max(
        len(mdl_coverage.get("_preserve_unclaimed_offsets", [])),
        len(converted_coverage.get("_preserve_unclaimed_offsets", [])),
    )
    if preserved_total:
        message += f"; preserved {preserved_total} unclaimed byte(s) verbatim"
    if external_ani:
        message += "; WARNING: external .ani copied unchanged, not parsed by ValveMDL"
    return ConversionResult(
        rel, "converted", message, len(jiggles), boing_flags_removed,
        surfaceprop_fields_cleared, external_ani
    )



# ---------------- Episode One / Source-2004 MDL v44 translation ----------------

from dataclasses import dataclass as _dataclass
import math as _math

LEGACY_V44_ANIMDESC_SIZE = 100
LEGACY_V44_SEQDESC_SIZE = 212
LEGACY_V44_IKRULE_SIZE = 152
LEGACY_V44_MOVEMENT_SIZE = 44
LEGACY_V44_ANIMBLOCK_SIZE = 8
LEGACY_V44_ANIM_FLAGS_MASK = 0x1F
LEGACY_V44_ANIM_RAWPOS = 0x01
LEGACY_V44_ANIM_RAWROT = 0x02
LEGACY_V44_ANIM_ANIMPOS = 0x04
LEGACY_V44_ANIM_ANIMROT = 0x08

# These records keep their sizes between the Episode One v44 and v49 studio
# layouts. Their tail fields do not all keep the same meaning, however.
LEGACY_V44_BODY_PART_SIZE = 16
LEGACY_V44_MODEL_SIZE = 148
LEGACY_V44_MESH_SIZE = 116
LEGACY_V44_FLEX_SIZE = 60
LEGACY_V44_EYEBALL_SIZE = 172
LEGACY_V44_VERTANIM_SIZE = 16
V49_WRINKLE_VERTANIM_SIZE = 18


@_dataclass
class EpisodeOneV44Report:
    bones: int
    animations: int
    sequences: int
    ik_rules: int
    procedural_bones: int
    animation_tracks: int
    flexes: int
    flex_vertices: int
    eyeballs: int
    mouths: int
    flex_type_fields_normalized: int
    hitbox_name_pointers_rebased: int
    sequence_anim_refs_outside_local_table: int
    include_models: tuple[str, ...]
    patch_ranges: tuple[tuple[int, int, str], ...]
    patch_values: tuple[tuple[int, bytes, str], ...]


def _checked_range(data: bytes | bytearray, offset: int, size: int, label: str,
                   *, allow_header: bool = False) -> None:
    if size < 0 or offset < 0:
        raise ConversionError(f"{label} has a negative offset/size.")
    if not allow_header and size and offset < MDL_HEADER_SIZE:
        raise ConversionError(f"{label} points into the MDL header (0x{offset:X}).")
    if offset + size > len(data):
        raise ConversionError(
            f"{label} range 0x{offset:X}-0x{offset + size:X} exceeds MDL length 0x{len(data):X}."
        )


def _root_string(data: bytes | bytearray, rel: int, label: str,
                 *, allow_zero: bool = True) -> str:
    if rel == 0 and allow_zero:
        return ""
    if rel <= 0:
        raise ConversionError(f"{label} has invalid relative string offset {rel}.")
    _checked_range(data, rel, 1, label)
    end = data.find(b"\0", rel)
    if end < 0:
        raise ConversionError(f"{label} at 0x{rel:X} is not NUL-terminated.")
    try:
        return data[rel:end].decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        # Source model names are usually ASCII, but legacy content can contain
        # Windows code-page bytes. Validate termination and keep a printable view.
        return data[rel:end].decode("latin-1")


def _relative_string(data: bytes | bytearray, base: int, rel: int, label: str,
                     *, allow_zero: bool = True) -> str:
    if rel == 0 and allow_zero:
        return ""
    if rel <= 0:
        raise ConversionError(f"{label} has invalid relative string offset {rel}.")
    return _root_string(data, base + rel, label, allow_zero=False)


def _relative_range(data: bytes | bytearray, base: int, rel: int, size: int,
                    label: str, *, allow_zero: bool = True) -> int:
    if size == 0:
        return base + max(rel, 0)
    if rel == 0 and allow_zero:
        raise ConversionError(f"{label} has a non-zero count but a null offset.")
    if rel <= 0:
        raise ConversionError(f"{label} has invalid relative offset {rel}.")
    absolute = base + rel
    _checked_range(data, absolute, size, label)
    return absolute


def _table(data: bytes | bytearray, count_offset: int, index_offset: int,
           record_size: int, label: str, *, maximum: int = 1_000_000) -> tuple[int, int]:
    count = _i32(data, count_offset)
    index = _i32(data, index_offset)
    if count < 0 or count > maximum:
        raise ConversionError(f"{label} has unreasonable count {count}.")
    if count:
        if index < MDL_HEADER_SIZE:
            raise ConversionError(f"{label} has invalid start offset 0x{index:X}.")
        _checked_range(data, index, count * record_size, label)
    return count, index


def _decode_animvalue_stream(data: bytes | bytearray, start: int,
                              frame_count: int, label: str) -> int:
    """Bounds-check one Source run-length encoded animation component."""
    if frame_count <= 0:
        return start
    cursor = start
    decoded_frames = 0
    run_count = 0
    while decoded_frames < frame_count:
        _checked_range(data, cursor, 2, f"{label} run header")
        valid, total = data[cursor], data[cursor + 1]
        cursor += 2
        if total == 0 or valid > total:
            raise ConversionError(
                f"{label} has invalid animation run (valid={valid}, total={total})."
            )
        value_bytes = valid * 2
        _checked_range(data, cursor, value_bytes, f"{label} run values")
        cursor += value_bytes
        decoded_frames += total
        run_count += 1
        if run_count > frame_count + 256:
            raise ConversionError(f"{label} has too many animation value runs.")
    return cursor


def _inspect_local_animation(data: bytes | bytearray, first_anim: int,
                             frame_count: int, bone_count: int,
                             label: str) -> int:
    """Parse the legacy mstudioanim_t linked list and its compressed value tracks."""
    if first_anim == 0:
        return 0
    current = first_anim
    visited: set[int] = set()
    tracks = 0
    while current:
        if current in visited:
            raise ConversionError(f"{label} animation record chain contains a cycle.")
        visited.add(current)
        if len(visited) > max(4096, bone_count * 4):
            raise ConversionError(f"{label} animation chain is unreasonably long.")
        _checked_range(data, current, 4, f"{label} animation record")
        bone = data[current]
        # Source's mstudioanim_t list ends with bone == 255. The engine exits
        # before reading flags, nextoffset, or payload for this sentinel; it is
        # not a bone index and carries no animation data. Static props commonly
        # contain a single FF 00 00 00 node.
        if bone == 0xFF:
            break
        flags = data[current + 1]
        next_offset = struct.unpack_from("<h", data, current + 2)[0]
        if bone >= bone_count:
            raise ConversionError(f"{label} animation references invalid bone {bone}.")
        unsupported_flags = flags & ~LEGACY_V44_ANIM_FLAGS_MASK
        if unsupported_flags & 0x20:  # STUDIO_ANIM_RAWROT2 (Quaternion64), later Source layout
            raise ConversionError(
                f"{label} animation uses STUDIO_ANIM_RAWROT2 (0x20), a later Source animation flag; "
                "retrying this file with --modern-v44 is appropriate."
            )
        if unsupported_flags:
            raise ConversionError(
                f"{label} animation has unsupported legacy flags 0x{flags:02X}; "
                "this converter will not guess a newer animation encoding."
            )
        if next_offset < 0:
            raise ConversionError(f"{label} animation has negative nextoffset {next_offset}.")

        cursor = current + 4
        if flags & LEGACY_V44_ANIM_RAWROT:
            cursor += 6  # Quaternion48
        if flags & LEGACY_V44_ANIM_RAWPOS:
            cursor += 6  # Vector48
        pointer_tables: list[tuple[str, int]] = []
        if flags & LEGACY_V44_ANIM_ANIMROT:
            pointer_tables.append(("rotation", cursor))
            cursor += 6  # three int16 offsets
        if flags & LEGACY_V44_ANIM_ANIMPOS:
            pointer_tables.append(("position", cursor))
            cursor += 6
        _checked_range(data, current + 4, cursor - (current + 4),
                       f"{label} animation payload header")

        for channel, pointer_table in pointer_tables:
            offsets = struct.unpack_from("<hhh", data, pointer_table)
            for axis, relative in enumerate(offsets):
                if relative > 0:
                    value_start = pointer_table + relative
                    _decode_animvalue_stream(
                        data, value_start, frame_count,
                        f"{label} {channel}[{axis}]"
                    )
                    tracks += 1

        if next_offset == 0:
            break
        if next_offset < (cursor - current):
            raise ConversionError(
                f"{label} animation nextoffset {next_offset} overlaps its record header."
            )
        next_record = current + next_offset
        _checked_range(data, next_record, 4, f"{label} next animation record")
        current = next_record
    return tracks


def _inspect_procedural_bones(data: bytes | bytearray, bone_count: int,
                              bone_index: int) -> int:
    count = 0
    for i in range(bone_count):
        bone_off = bone_index + i * BONE_SIZE
        name_rel = _i32(data, bone_off)
        _relative_string(data, bone_off, name_rel, f"bone {i} name", allow_zero=False)
        parent = _i32(data, bone_off + 4)
        if parent < -1 or parent >= bone_count or parent == i:
            raise ConversionError(f"Bone {i} has invalid parent index {parent}.")
        proc_type = _i32(data, bone_off + BONE_PROCTYPE_OFFSET)
        proc_rel = _i32(data, bone_off + BONE_PROCINDEX_OFFSET)
        surfaceprop_rel = _i32(data, bone_off + BONE_SURFACEPROP_OFFSET)
        if surfaceprop_rel < 0:
            raise ConversionError(f"Bone {i} has negative surfaceprop offset.")
        _relative_string(data, bone_off, surfaceprop_rel, f"bone {i} surface property")
        if proc_type == 0:
            if proc_rel != 0:
                raise ConversionError(f"Bone {i} has procindex but no procedural type.")
            continue
        if proc_type not in (1, 2, 3, 4):
            raise ConversionError(
                f"Bone {i} uses procedural type {proc_type}, which is not part of the verified Episode One v44 schema."
            )
        if proc_rel <= 0:
            raise ConversionError(f"Bone {i} has procedural type {proc_type} but no record offset.")
        proc_off = bone_off + proc_rel
        if proc_type == 1:
            _checked_range(data, proc_off, 176, f"bone {i} axis-interp record")
        elif proc_type == 2:
            _checked_range(data, proc_off, 12, f"bone {i} quat-interp record")
            control, trigger_count, trigger_rel = struct.unpack_from("<iii", data, proc_off)
            if control < 0 or control >= bone_count:
                raise ConversionError(f"Bone {i} quat-interp references invalid control bone {control}.")
            if trigger_count < 0 or trigger_count > 4096:
                raise ConversionError(f"Bone {i} has unreasonable quat-interp trigger count {trigger_count}.")
            if trigger_count:
                trigger_off = _relative_range(
                    data, proc_off, trigger_rel, trigger_count * 48,
                    f"bone {i} quat-interp triggers", allow_zero=False
                )
                if trigger_off < MDL_HEADER_SIZE:
                    raise ConversionError(f"Bone {i} quat-interp triggers point into the header.")
        else:
            # AIMATBONE and AIMATATTACH share the 44-byte aim record layout.
            _checked_range(data, proc_off, 44, f"bone {i} aim procedural record")
        count += 1
    return count


def _is_valid_flexdesc_index(value: int, flexdesc_count: int) -> bool:
    """Flex references use -1 for an absent target; otherwise they are indexes."""
    return value == -1 or 0 <= value < flexdesc_count


def _inspect_legacy_face_model_tree(
    data: bytes | bytearray,
    bodypart_count: int,
    bodypart_index: int,
    bone_count: int,
    flexdesc_count: int,
    expected_version: int,
    patch_values: list[tuple[int, bytes, str]],
) -> tuple[int, int, int]:
    """Validate model/mesh flexes and eyeballs; normalize the v49 flex discriminator.

    Episode One's 60-byte ``mstudioflex_t`` ended in ``unused[7]``. The v49
    record is still 60 bytes, but reuses the first four bytes of that tail as
    ``vertanimtype`` plus three padding bytes. In v44 these bytes have no wrinkle
    discriminator, so they must be written as zero (normal 16-byte vertex
    animation records) before a v49 engine traverses the flex vertex array.

    The rest of each flex/eyeball record and every vertex-delta payload is left
    byte-for-byte unchanged. This walker also validates the model-local eye
    flex references, which the root-table-only legacy parser previously missed.
    """
    flex_count = 0
    flex_vertex_count = 0
    eyeball_count = 0
    bodypart_record_offsets = _table_array_for_face_tree(
        data, bodypart_index, bodypart_count, LEGACY_V44_BODY_PART_SIZE,
        "body parts"
    )

    for bodypart_number, bodypart_off in enumerate(bodypart_record_offsets):
        name_rel = _i32(data, bodypart_off)
        _relative_string(data, bodypart_off, name_rel,
                         f"body part {bodypart_number} name", allow_zero=False)
        model_count = _i32(data, bodypart_off + 4)
        model_rel = _i32(data, bodypart_off + 12)
        if model_count < 0 or model_count > 65536:
            raise ConversionError(
                f"Body part {bodypart_number} has unreasonable model count {model_count}."
            )
        if not model_count:
            continue
        model_start = _relative_range(
            data, bodypart_off, model_rel,
            model_count * LEGACY_V44_MODEL_SIZE,
            f"body part {bodypart_number} models", allow_zero=False,
        )

        for model_number in range(model_count):
            model_off = model_start + model_number * LEGACY_V44_MODEL_SIZE
            model_label = f"body part {bodypart_number} model {model_number}"
            mesh_count, mesh_rel = struct.unpack_from("<ii", data, model_off + 72)
            if mesh_count < 0 or mesh_count > 65536:
                raise ConversionError(f"{model_label} has unreasonable mesh count {mesh_count}.")
            if mesh_count:
                mesh_start = _relative_range(
                    data, model_off, mesh_rel,
                    mesh_count * LEGACY_V44_MESH_SIZE,
                    f"{model_label} meshes", allow_zero=False,
                )
            else:
                mesh_start = 0

            attachment_count, attachment_rel = struct.unpack_from(
                "<ii", data, model_off + 92
            )
            if attachment_count < 0 or attachment_count > 65536:
                raise ConversionError(
                    f"{model_label} has unreasonable attachment count {attachment_count}."
                )
            if attachment_count:
                _relative_range(
                    data, model_off, attachment_rel, attachment_count * 92,
                    f"{model_label} attachments", allow_zero=False,
                )

            eye_count, eye_rel = struct.unpack_from("<ii", data, model_off + 100)
            if eye_count < 0 or eye_count > 4096:
                raise ConversionError(f"{model_label} has unreasonable eyeball count {eye_count}.")
            if eye_count:
                eye_start = _relative_range(
                    data, model_off, eye_rel,
                    eye_count * LEGACY_V44_EYEBALL_SIZE,
                    f"{model_label} eyeballs", allow_zero=False,
                )
                for eye_number in range(eye_count):
                    eye_off = eye_start + eye_number * LEGACY_V44_EYEBALL_SIZE
                    eye_label = f"{model_label} eyeball {eye_number}"
                    # sznameindex is optional in shipped Source models. A zero
                    # offset means the eyeball has no separate name string; do
                    # not interpret the start of the record as an unterminated
                    # name or reject otherwise valid HL2 character models.
                    _relative_string(
                        data, eye_off, _i32(data, eye_off),
                        f"{eye_label} name", allow_zero=True,
                    )
                    eye_bone = _i32(data, eye_off + 4)
                    if eye_bone < 0 or eye_bone >= bone_count:
                        raise ConversionError(
                            f"{eye_label} references invalid bone {eye_bone}."
                        )
                    # Each eye contains three upper and three lower eyelid control
                    # flexes, followed by the two actual lid-geometry flexes.
                    for field, slot_count, field_label in (
                        (68, 3, "upper lid"),
                        (80, 3, "lower lid"),
                        (116, 1, "upper lid geometry"),
                        (120, 1, "lower lid geometry"),
                    ):
                        for slot in range(slot_count):
                            value = _i32(data, eye_off + field + slot * 4)
                            if not _is_valid_flexdesc_index(value, flexdesc_count):
                                raise ConversionError(
                                    f"{eye_label} {field_label} flex reference {value} "
                                    f"is outside {flexdesc_count} flex descriptions."
                                )
                    non_facs = data[eye_off + 140]
                    if expected_version >= 44 and non_facs not in (0, 1):
                        raise ConversionError(
                            f"{eye_label} has invalid m_bNonFACS byte 0x{non_facs:02X}."
                        )
                    if expected_version < 44 and non_facs:
                        # The flag did not exist in v43. In that experimental
                        # compatibility mode this byte is legacy reserved data.
                        patch_values.append((
                            eye_off + 140, b"\0", f"{eye_label} absent pre-v44 m_bNonFACS flag"
                        ))
                    eyeball_count += 1

            for mesh_number in range(mesh_count):
                mesh_off = mesh_start + mesh_number * LEGACY_V44_MESH_SIZE
                mesh_label = f"{model_label} mesh {mesh_number}"
                model_back_rel = _i32(data, mesh_off + 4)
                if mesh_off + model_back_rel != model_off:
                    raise ConversionError(
                        f"{mesh_label} modelindex does not point back to its owning model."
                    )
                mesh_vertices = _i32(data, mesh_off + 8)
                if mesh_vertices < 0 or mesh_vertices > 1_000_000:
                    raise ConversionError(
                        f"{mesh_label} has unreasonable vertex count {mesh_vertices}."
                    )
                mesh_flex_count, mesh_flex_rel = struct.unpack_from(
                    "<ii", data, mesh_off + 16
                )
                if mesh_flex_count < 0 or mesh_flex_count > 65536:
                    raise ConversionError(
                        f"{mesh_label} has unreasonable flex count {mesh_flex_count}."
                    )
                if not mesh_flex_count:
                    continue
                flex_start = _relative_range(
                    data, mesh_off, mesh_flex_rel,
                    mesh_flex_count * LEGACY_V44_FLEX_SIZE,
                    f"{mesh_label} flex records", allow_zero=False,
                )
                for flex_number in range(mesh_flex_count):
                    flex_off = flex_start + flex_number * LEGACY_V44_FLEX_SIZE
                    flex_label = f"{mesh_label} flex {flex_number}"
                    flexdesc = _i32(data, flex_off)
                    if flexdesc < 0 or flexdesc >= flexdesc_count:
                        raise ConversionError(
                            f"{flex_label} references invalid flex description {flexdesc}."
                        )
                    targets = struct.unpack_from("<ffff", data, flex_off + 4)
                    if not all(_math.isfinite(value) for value in targets):
                        raise ConversionError(f"{flex_label} has a non-finite target weight.")
                    numverts = _i32(data, flex_off + 20)
                    vert_rel = _i32(data, flex_off + 24)
                    flexpair = _i32(data, flex_off + 28)
                    if numverts < 0 or numverts > 1_000_000:
                        raise ConversionError(
                            f"{flex_label} has unreasonable vertex-animation count {numverts}."
                        )
                    if flexpair != -1 and not _is_valid_flexdesc_index(flexpair, flexdesc_count):
                        raise ConversionError(
                            f"{flex_label} references invalid paired flex description {flexpair}."
                        )

                    if expected_version == TARGET_VERSION:
                        vertanim_type = data[flex_off + 32]
                        if vertanim_type not in (0, 1):
                            raise ConversionError(
                                f"{flex_label} has unknown v49 vertanimtype {vertanim_type}."
                            )
                        vertanim_size = (
                            LEGACY_V44_VERTANIM_SIZE if vertanim_type == 0
                            else V49_WRINKLE_VERTANIM_SIZE
                        )
                    else:
                        # v44 has no vertanimtype; its vertex records are the
                        # standard 16-byte mstudiovertanim_t form.
                        vertanim_size = LEGACY_V44_VERTANIM_SIZE
                        if any(data[flex_off + 32:flex_off + 36]):
                            patch_values.append((
                                flex_off + 32, b"\0\0\0\0",
                                f"{flex_label} legacy unused tail -> v49 normal vertanimtype",
                            ))

                    if numverts:
                        vert_start = _relative_range(
                            data, flex_off, vert_rel, numverts * vertanim_size,
                            f"{flex_label} vertex-animation data", allow_zero=False,
                        )
                        # Check each vertex delta's index against its mesh, but
                        # do not decode/re-encode the compressed delta payload.
                        for vert_number in range(numverts):
                            vertex_index = struct.unpack_from(
                                "<H", data, vert_start + vert_number * vertanim_size
                            )[0]
                            if vertex_index >= mesh_vertices:
                                raise ConversionError(
                                    f"{flex_label} vertex delta {vert_number} references "
                                    f"mesh vertex {vertex_index}, but the mesh has {mesh_vertices}."
                                )
                        flex_vertex_count += numverts
                    flex_count += 1

    return flex_count, flex_vertex_count, eyeball_count


def _table_array_for_face_tree(
    data: bytes | bytearray, index: int, count: int, record_size: int, label: str
) -> list[int]:
    """Return validated root-record offsets for the model tree."""
    if count == 0:
        return []
    _checked_range(data, index, count * record_size, label)
    return [index + i * record_size for i in range(count)]


def inspect_episode_one_v44(data: bytes, *, expected_version: int = 44,
                             source_path: Path | None = None) -> EpisodeOneV44Report:
    """Parse and validate the Source-2004 / Episode One studio format.

    The main header remains 408 bytes, but its old tail and several descriptors
    reuse slots that later Source versions assign to different features. This
    parser validates the offsets actually followed by the conversion and the
    complete local compressed animation record chains before any bytes are changed.
    """
    if len(data) < MDL_HEADER_SIZE or data[:4] != MDL_MAGIC:
        raise ConversionError("Not a complete IDST studio model.")
    version, checksum = struct.unpack_from("<ii", data, 4)
    declared_length = _i32(data, 76)
    if version != expected_version:
        raise ConversionError(f"Episode One parser expected v{expected_version}, found v{version}.")
    if declared_length != len(data):
        raise ConversionError(f"MDL header length {declared_length} does not match file size {len(data)}.")
    if not any(data[12:76]):
        raise ConversionError("MDL header has an empty model name.")

    # studiohdr_t table pairs from Valve's Episode One / Source-2006 studio.h.
    root_tables = (
        (156, 160, 216, "bones", 1024),
        (164, 168, 56, "bone controllers", 4096),
        (172, 176, 12, "hitbox sets", 4096),
        (180, 184, LEGACY_V44_ANIMDESC_SIZE, "local animations", 65536),
        (188, 192, LEGACY_V44_SEQDESC_SIZE, "local sequences", 65536),
        (204, 208, 64, "textures", 65536),
        (212, 216, 4, "texture search paths", 4096),
        (232, 236, 16, "body parts", 4096),
        (240, 244, 92, "attachments", 4096),
        (260, 264, 4, "flex descriptions", 65536),
        (268, 272, 20, "flex controllers", 4096),
        (276, 280, 12, "flex rules", 65536),
        (284, 288, 16, "IK chains", 4096),
        (292, 296, 20, "mouths", 4096),
        (300, 304, 20, "local pose parameters", 4096),
        (320, 324, 32, "local IK autoplay locks", 4096),
        (336, 340, 8, "included model groups", 4096),
        (352, 356, 8, "animation blocks", 4096),
    )
    arrays: dict[str, tuple[int, int]] = {}
    for count_off, index_off, size, label, maximum in root_tables:
        arrays[label] = _table(data, count_off, index_off, size, label, maximum=maximum)

    bone_count, bone_index = arrays["bones"]
    anim_count, anim_index = arrays["local animations"]
    seq_count, seq_index = arrays["local sequences"]
    bodypart_count, bodypart_index = arrays["body parts"]
    flexdesc_count, flexdesc_index = arrays["flex descriptions"]
    ik_chain_count, ik_chain_index = arrays["IK chains"]

    # Header-level strings, raw arrays, and legacy-only root tail.
    surfaceprop = _i32(data, 308)
    _root_string(data, surfaceprop, "studiohdr_t.surfacepropindex")
    keyvalue_index, keyvalue_size = _i32(data, 312), _i32(data, 316)
    if keyvalue_size < 0:
        raise ConversionError("studiohdr_t.keyvaluesize is negative.")
    if keyvalue_size:
        if keyvalue_index <= 0:
            raise ConversionError("studiohdr_t has keyvaluesize but no keyvalueindex.")
        _checked_range(data, keyvalue_index, keyvalue_size, "studiohdr_t keyvalues")
    for rel, label in ((_i32(data, 348), "animation block name"),):
        _root_string(data, rel, f"studiohdr_t {label}")

    skin_refs, skin_families, skin_index = (
        _i32(data, 220), _i32(data, 224), _i32(data, 228)
    )
    if skin_refs < 0 or skin_families < 0 or skin_refs > 65536 or skin_families > 65536:
        raise ConversionError("Skin table has unreasonable dimensions.")
    if skin_refs and skin_families:
        _checked_range(data, skin_index, skin_refs * skin_families * 2, "skin reference table")

    node_count, node_transition_index, node_name_index = (
        _i32(data, 248), _i32(data, 252), _i32(data, 256)
    )
    if node_count < 0 or node_count > 4096:
        raise ConversionError(f"Unreasonable local node count {node_count}.")
    if node_count:
        _checked_range(data, node_transition_index, node_count * node_count,
                       "local node transition matrix")
        _checked_range(data, node_name_index, node_count * 4, "local node name offsets")
        for n in range(node_count):
            rel = _i32(data, node_name_index + n * 4)
            _root_string(data, rel, f"local node {n} name", allow_zero=False)

    # Validate simple root-table strings and the nested records we need to
    # preserve: material names, hitboxes, attachment bones, flex/IK metadata.
    patch_values: list[tuple[int, bytes, str]] = []

    texture_count, texture_index = arrays["textures"]
    for i in range(texture_count):
        off = texture_index + i * 64
        _relative_string(data, off, _i32(data, off), f"texture {i} name", allow_zero=False)

    cd_count, cd_index = arrays["texture search paths"]
    for i in range(cd_count):
        rel = _i32(data, cd_index + i * 4)
        _root_string(data, rel, f"texture search path {i}", allow_zero=False)

    hitbox_count, hitbox_index = arrays["hitbox sets"]
    for i in range(hitbox_count):
        off = hitbox_index + i * 12
        _relative_string(data, off, _i32(data, off), f"hitbox set {i} name", allow_zero=False)
        num_boxes, box_rel = struct.unpack_from("<ii", data, off + 4)
        if num_boxes < 0 or num_boxes > 65536:
            raise ConversionError(f"Hitbox set {i} has unreasonable box count {num_boxes}.")
        if num_boxes:
            box_off = _relative_range(data, off, box_rel, num_boxes * 68,
                                      f"hitbox set {i} records", allow_zero=False)
            for j in range(num_boxes):
                hb = box_off + j * 68
                bone = _i32(data, hb)
                if bone < 0 or bone >= bone_count:
                    raise ConversionError(f"Hitbox {i}:{j} references invalid bone {bone}.")
                name_rel = _i32(data, hb + 32)
                if expected_version == TARGET_VERSION:
                    # Modern studio hitbox names are relative to each bbox record.
                    _relative_string(data, hb, name_rel, f"hitbox {i}:{j} name")
                elif name_rel:
                    # Episode One assets exist with this field stored as an
                    # absolute file offset. Rebase only when treating it as a
                    # record-relative pointer would leave the file; otherwise
                    # retain the normal SDK-relative interpretation.
                    if name_rel > 0 and name_rel < len(data) and hb + name_rel >= len(data):
                        _root_string(data, name_rel, f"hitbox {i}:{j} legacy absolute name", allow_zero=False)
                        patch_values.append((
                            hb + 32,
                            struct.pack("<i", name_rel - hb),
                            f"hitbox {i}:{j} absolute name pointer -> relative",
                        ))
                    else:
                        _relative_string(data, hb, name_rel, f"hitbox {i}:{j} name")

    attach_count, attach_index = arrays["attachments"]
    for i in range(attach_count):
        off = attach_index + i * 92
        _relative_string(data, off, _i32(data, off), f"attachment {i} name", allow_zero=False)
        bone = _i32(data, off + 8)
        if bone < 0 or bone >= bone_count:
            raise ConversionError(f"Attachment {i} references invalid bone {bone}.")

    for i in range(flexdesc_count):
        off = flexdesc_index + i * 4
        _relative_string(data, off, _i32(data, off), f"flex description {i} name", allow_zero=False)

    flexctrl_count, flexctrl_index = arrays["flex controllers"]
    for i in range(flexctrl_count):
        off = flexctrl_index + i * 20
        _relative_string(data, off, _i32(data, off), f"flex controller {i} type", allow_zero=False)
        _relative_string(data, off, _i32(data, off + 4), f"flex controller {i} name", allow_zero=False)

    flexrule_count, flexrule_index = arrays["flex rules"]
    for i in range(flexrule_count):
        off = flexrule_index + i * 12
        flex, op_count, op_rel = struct.unpack_from("<iii", data, off)
        if flex < 0 or flex >= flexdesc_count:
            raise ConversionError(f"Flex rule {i} references invalid flex {flex}.")
        if op_count < 0 or op_count > 65536:
            raise ConversionError(f"Flex rule {i} has unreasonable op count {op_count}.")
        if op_count:
            _relative_range(data, off, op_rel, op_count * 8,
                            f"flex rule {i} operations", allow_zero=False)

    flex_count, flex_vertex_count, eyeball_count = _inspect_legacy_face_model_tree(
        data, bodypart_count, bodypart_index, bone_count, flexdesc_count,
        expected_version, patch_values,
    )

    for i in range(ik_chain_count):
        off = ik_chain_index + i * 16
        name_rel, _link_type, link_count, link_rel = struct.unpack_from("<iiii", data, off)
        _relative_string(data, off, name_rel, f"IK chain {i} name", allow_zero=False)
        if link_count < 0 or link_count > bone_count:
            raise ConversionError(f"IK chain {i} has unreasonable link count {link_count}.")
        if link_count:
            link_off = _relative_range(data, off, link_rel, link_count * 28,
                                       f"IK chain {i} links", allow_zero=False)
            for j in range(link_count):
                link_bone = _i32(data, link_off + j * 28)
                if link_bone < 0 or link_bone >= bone_count:
                    raise ConversionError(f"IK chain {i} link {j} has invalid bone {link_bone}.")

    mouth_count, mouth_index = arrays["mouths"]
    for i in range(mouth_count):
        off = mouth_index + i * 20
        bone, = struct.unpack_from("<i", data, off)
        if bone < 0 or bone >= bone_count:
            raise ConversionError(f"Mouth {i} references invalid bone {bone}.")
        forward = struct.unpack_from("<fff", data, off + 4)
        if not all(_math.isfinite(value) for value in forward):
            raise ConversionError(f"Mouth {i} has a non-finite forward vector.")
        flexdesc = _i32(data, off + 16)
        if not _is_valid_flexdesc_index(flexdesc, flexdesc_count):
            raise ConversionError(
                f"Mouth {i} references invalid phoneme flex description {flexdesc}."
            )

    pose_count, pose_index = arrays["local pose parameters"]
    for i in range(pose_count):
        off = pose_index + i * 20
        _relative_string(data, off, _i32(data, off), f"pose parameter {i} name", allow_zero=False)

    include_count, include_index = arrays["included model groups"]
    include_names: list[str] = []
    for i in range(include_count):
        off = include_index + i * 8
        label_rel, name_rel = struct.unpack_from("<ii", data, off)
        _relative_string(data, off, label_rel, f"included model {i} label")
        name = _relative_string(data, off, name_rel, f"included model {i} name", allow_zero=False)
        include_names.append(name)

    animblock_count, animblock_index = arrays["animation blocks"]
    if animblock_count:
        # Blocks are 1-based at runtime; index 0 is the empty block.
        for i in range(animblock_count):
            start, end = struct.unpack_from("<ii", data, animblock_index + i * 8)
            if start < 0 or end < start:
                raise ConversionError(f"Animation block {i} has invalid data range {start}..{end}.")

    external_ani_needed = False
    if animblock_count:
        for i in range(anim_count):
            off = anim_index + i * LEGACY_V44_ANIMDESC_SIZE
            block = _i32(data, off + 52)
            if block > 0:
                external_ani_needed = True
    if external_ani_needed:
        if source_path is None:
            raise ConversionError("This model references external animation blocks; source path is needed to validate its .ani file.")
        ani_path = source_path.with_suffix(".ani")
        if not ani_path.is_file():
            raise ConversionError(f"External animation file {ani_path.name} is required but missing.")
        ani_data = ani_path.read_bytes()
        if len(ani_data) < 8 or ani_data[:4] != b"IDAG":
            raise ConversionError(f"External animation file {ani_path.name} has an invalid IDAG header.")

    procedural_count = _inspect_procedural_bones(data, bone_count, bone_index)
    patch_ranges: list[tuple[int, int, str]] = []
    animation_tracks = 0
    total_ik_rules = 0
    sequence_anim_refs_outside_local_table = 0

    # Legacy animation descriptor: bbox vectors become modern unused1[6]; the
    # final seven reserved ints became local hierarchy/section/zero-frame fields.
    for i in range(anim_count):
        off = anim_index + i * LEGACY_V44_ANIMDESC_SIZE
        baseptr, name_rel = struct.unpack_from("<ii", data, off)
        if baseptr != -off:
            raise ConversionError(f"Animation descriptor {i} has invalid baseptr {baseptr} (expected {-off}).")
        _relative_string(data, off, name_rel, f"animation {i} name", allow_zero=False)
        fps = struct.unpack_from("<f", data, off + 8)[0]
        flags, frames, movement_count, movement_rel = struct.unpack_from("<iiii", data, off + 12)
        if not _math.isfinite(fps) or fps < 0:
            raise ConversionError(f"Animation {i} has invalid fps {fps}.")
        if frames < 0 or frames > 1_000_000:
            raise ConversionError(f"Animation {i} has unreasonable frame count {frames}.")
        if movement_count < 0 or movement_count > 65536:
            raise ConversionError(f"Animation {i} has unreasonable movement count {movement_count}.")
        if movement_count:
            _relative_range(data, off, movement_rel, movement_count * LEGACY_V44_MOVEMENT_SIZE,
                            f"animation {i} movement records", allow_zero=False)
        block, anim_rel, ik_count, ik_rel, external_ik_rel = struct.unpack_from("<iiiii", data, off + 52)
        if block < 0 or block > animblock_count:
            raise ConversionError(f"Animation {i} references invalid animation block {block}.")
        if ik_count < 0 or ik_count > 65536:
            raise ConversionError(f"Animation {i} has unreasonable IK rule count {ik_count}.")
        if any(data[off + 72:off + 100]):
            raise ConversionError(
                f"Animation {i} uses modern local-hierarchy/section/zero-frame fields; "
                "the Episode One parser will not erase them. Use --modern-v44 when appropriate."
            )
        if external_ik_rel:
            raise ConversionError(
                f"Animation {i} stores IK rules in an external .ani block; that legacy IK table is not translated by this build."
            )
        if ik_count:
            ik_off = _relative_range(data, off, ik_rel, ik_count * LEGACY_V44_IKRULE_SIZE,
                                     f"animation {i} IK rules", allow_zero=False)
            total_ik_rules += ik_count
            for j in range(ik_count):
                rule_off = ik_off + j * LEGACY_V44_IKRULE_SIZE
                rule_type = _i32(data, rule_off + 4)
                rule_bone = _i32(data, rule_off + 12)
                if rule_type not in (0, 1, 2, 3, 4, 5, 6):#Adding a 0 to the allowed list fixes this, but why? - theaperturecat
                    raise ConversionError(f"Animation {i} IK rule {j} has unknown type {rule_type}.")
                if rule_bone >= bone_count or rule_bone < -1:
                    raise ConversionError(f"Animation {i} IK rule {j} references invalid bone {rule_bone}.")
                attachment_rel = _i32(data, rule_off + 120)
                _relative_string(data, rule_off, attachment_rel,
                                 f"animation {i} IK rule {j} attachment")
                compressed_error_rel = _i32(data, rule_off + 60)
                if compressed_error_rel:
                    _relative_range(data, rule_off, compressed_error_rel, 36,
                                    f"animation {i} IK rule {j} compressed error header")
                error_rel = _i32(data, rule_off + 72)
                # IK_RELEASE records have no error curve; legacy files commonly
                # carry a stale non-zero relative value in this unused slot.
                if error_rel and rule_type != 4:
                    _relative_range(data, rule_off, error_rel, 28,
                                    f"animation {i} IK rule {j} error data")
                # These old slots are reserved. Non-zero values can be modern
                # unused3/drop/top floats, so do not erase them blindly.
                if any(data[rule_off + 92:rule_off + 96]) or any(data[rule_off + 100:rule_off + 108]):
                    raise ConversionError(
                        f"Animation {i} IK rule {j} has non-zero legacy-reserved/modern IK fields; "
                        "refusing ambiguous conversion. Use --modern-v44 when appropriate."
                    )
                patch_ranges.extend((
                    (rule_off + 92, 4, f"animation {i} IK rule {j} unused3"),
                    (rule_off + 100, 8, f"animation {i} IK rule {j} unused4/unused5"),
                ))

        if block == 0 and anim_rel:
            first_anim = _relative_range(data, off, anim_rel, 4,
                                         f"animation {i} local data", allow_zero=False)
            animation_tracks += _inspect_local_animation(
                data, first_anim, frames, bone_count, f"animation {i}"
            )
        elif block == 0 and frames and not anim_rel:
            # A fully static animation may have no records; that's valid.
            pass
        elif block > 0:
            # The animation payload itself is retained byte-for-byte in .ani.
            # All local descriptors and IK records remain in the MDL.
            pass

        patch_ranges.append((off + 28, 24, f"animation {i} legacy bbox -> v49 reserved"))
        patch_ranges.append((off + 72, 28, f"animation {i} legacy tail -> v49 section fields"))

    # Sequence descriptor remains 212 bytes, but old unused[8] became three
    # modern fields plus unused[5]. The entire old reserved tail is cleared.
    for i in range(seq_count):
        off = seq_index + i * LEGACY_V44_SEQDESC_SIZE
        baseptr, label_rel, activity_rel = struct.unpack_from("<iii", data, off)
        if baseptr != -off:
            raise ConversionError(f"Sequence descriptor {i} has invalid baseptr {baseptr} (expected {-off}).")
        _relative_string(data, off, label_rel, f"sequence {i} label", allow_zero=False)
        _relative_string(data, off, activity_rel, f"sequence {i} activity name")
        num_events, event_rel = struct.unpack_from("<ii", data, off + 24)
        if num_events < 0 or num_events > 65536:
            raise ConversionError(f"Sequence {i} has unreasonable event count {num_events}.")
        if num_events:
            _relative_range(data, off, event_rel, num_events * 80,
                            f"sequence {i} events", allow_zero=False)
        num_blends = _i32(data, off + 56)
        animindex_rel = _i32(data, off + 60)
        group_x, group_y = struct.unpack_from("<ii", data, off + 68)
        if num_blends < 0 or num_blends > 65536 or group_x < 0 or group_y < 0:
            raise ConversionError(f"Sequence {i} has invalid blend dimensions.")
        blend_entries = group_x * group_y
        if blend_entries > 65536:
            raise ConversionError(f"Sequence {i} has unreasonable blend matrix size {blend_entries}.")
        if blend_entries:
            blend_off = _relative_range(data, off, animindex_rel, blend_entries * 2,
                                        f"sequence {i} animation index matrix", allow_zero=False)
            for j in range(blend_entries):
                anim_num = struct.unpack_from("<h", data, blend_off + j * 2)[0]
                if anim_num < -1:
                    raise ConversionError(
                        f"Sequence {i} blend {j} has invalid negative animation index {anim_num}."
                    )
                if anim_num >= anim_count:
                    # The serialized sequence matrix stores an animation index,
                    # not a direct byte pointer. Source's pLocalAnimdesc() clamps
                    # an out-of-range local index to zero, while a virtual model
                    # can remap sequence-relative indices into included groups.
                    # Preserve the stored short exactly; validating it against
                    # this file's local animation count alone rejects compatible
                    # legacy content (e.g. HL2's animated breakable Smokestack).
                    # If the file has no local animations and no included model
                    # groups at all, however, there is no possible local fallback.
                    if anim_count == 0 and include_count == 0:
                        raise ConversionError(
                            f"Sequence {i} blend {j} references animation {anim_num}, "
                            "but the model has no local animations or included model groups."
                        )
                    sequence_anim_refs_outside_local_table += 1
        movement_rel = _i32(data, off + 64)
        if num_blends and movement_rel:
            _relative_range(data, off, movement_rel, num_blends * LEGACY_V44_MOVEMENT_SIZE,
                            f"sequence {i} movement data")
        num_layers, layer_rel = struct.unpack_from("<ii", data, off + 148)
        if num_layers < 0 or num_layers > 65536:
            raise ConversionError(f"Sequence {i} has unreasonable autolayer count {num_layers}.")
        if num_layers:
            _relative_range(data, off, layer_rel, num_layers * 24,
                            f"sequence {i} autolayers", allow_zero=False)
        weight_rel = _i32(data, off + 156)
        if weight_rel:
            _relative_range(data, off, weight_rel, bone_count * 4,
                            f"sequence {i} bone weight list")
        num_locks, lock_rel = struct.unpack_from("<ii", data, off + 164)
        if num_locks < 0 or num_locks > 65536:
            raise ConversionError(f"Sequence {i} has unreasonable IK lock count {num_locks}.")
        if num_locks:
            _relative_range(data, off, lock_rel, num_locks * 32,
                            f"sequence {i} IK locks", allow_zero=False)
        key_rel, key_size = struct.unpack_from("<ii", data, off + 172)
        if any(data[off + 180:off + 192]):
            raise ConversionError(
                f"Sequence {i} uses modern cycle-pose/activity-modifier fields; "
                "this is not a plain Episode One descriptor. Use --modern-v44 when appropriate."
            )
        if key_size < 0:
            raise ConversionError(f"Sequence {i} has negative keyvalue size.")
        if key_size:
            _relative_range(data, off, key_rel, key_size,
                            f"sequence {i} keyvalues", allow_zero=False)
        patch_ranges.append((off + 180, 32, f"sequence {i} legacy reserved tail -> v49 sequence fields"))

    # The entire old tail is repurposed by later studiohdr_t layouts:
    #   +378..379 numAllowedRootLODs/padding
    #   +380..383 unused4 (formerly the legacy zero-frame cache index)
    #   +384..399 flex-controller UI/scale fields
    #   +400..407 studiohdr2index and adjacent fields
    # The legacy writer clears this region, so require it to be entirely zero.
    # If it is not, try the modern v44 parser rather than destroying those values.
    if any(data[378:408]):
        raise ConversionError(
            "The Episode One header tail (+378..407) is non-zero in fields reused by "
            "modern v44+ studiohdr_t layouts. Use --modern-v44 for the modern parser."
        )
    patch_ranges.append((378, 30, "legacy studiohdr_t tail -> modern reserved/header2 fields"))

    # Verify no output patch ranges overlap one another; multiple IK rules can
    # be adjacent, but the actual byte intervals must remain disjoint.
    sorted_ranges = sorted(patch_ranges, key=lambda item: item[0])
    previous_end = -1
    for start, size, label in sorted_ranges:
        if start < previous_end:
            raise ConversionError(f"Internal parser produced overlapping patch ranges near {label}.")
        _checked_range(data, start, size, label, allow_header=True)
        previous_end = start + size

    # Value patches must also stay inside the file and must not collide with
    # descriptor/header ranges that are zeroed during translation.
    for offset, replacement, label in patch_values:
        _checked_range(data, offset, len(replacement), label, allow_header=True)
        if any(offset < start + size and start < offset + len(replacement)
               for start, size, _ in sorted_ranges):
            raise ConversionError(f"Internal parser produced overlapping patch operation: {label}.")

    hitbox_name_pointers_rebased = sum(
        1 for _offset, _replacement, label in patch_values
        if "absolute name pointer -> relative" in label
    )
    flex_type_fields_normalized = sum(
        1 for _offset, _replacement, label in patch_values
        if "legacy unused tail -> v49 normal vertanimtype" in label
    )
    return EpisodeOneV44Report(
        bones=bone_count,
        animations=anim_count,
        sequences=seq_count,
        ik_rules=total_ik_rules,
        procedural_bones=procedural_count,
        animation_tracks=animation_tracks,
        flexes=flex_count,
        flex_vertices=flex_vertex_count,
        eyeballs=eyeball_count,
        mouths=mouth_count,
        flex_type_fields_normalized=flex_type_fields_normalized,
        hitbox_name_pointers_rebased=hitbox_name_pointers_rebased,
        sequence_anim_refs_outside_local_table=sequence_anim_refs_outside_local_table,
        include_models=tuple(include_names),
        patch_ranges=tuple(sorted_ranges),
        patch_values=tuple(patch_values),
    )


def write_episode_one_v44_as_v49(source_mdl: Path, input_root: Path,
                                  output_root: Path, source_version: int,
                                  dry_run: bool, experimental_v43: bool = False,
                                  *, vtx_layout: str = "alien-swarm") -> ConversionResult:
    """Translate legacy Episode One descriptors/tails to the v49 interpretation."""
    rel = source_mdl.relative_to(input_root)
    original = source_mdl.read_bytes()
    report = inspect_episode_one_v44(
        original, expected_version=source_version, source_path=source_mdl
    )
    version, checksum, declared_length = read_mdl_header(source_mdl)
    if version != source_version or declared_length != len(original):
        raise ConversionError("Legacy MDL header changed during inspection.")
    validate_raw_sidecars(source_mdl, checksum)
    prepared_vtx, vtx_stats = _prepare_vtx_sidecars(
        source_mdl, input_root, output_root, checksum,
        dry_run=dry_run, layout=vtx_layout, source_version=source_version,
    )

    external_ani = source_mdl.with_suffix(".ani").exists()
    if dry_run:
        message = (
            f"would translate Episode One v{source_version} -> v49; "
            f"{report.bones} bones, {report.procedural_bones} procedural bones, "
            f"{report.animations} animations, {report.sequences} sequences, "
            f"{report.ik_rules} IK rules, {report.animation_tracks} compressed tracks; "
            f"{report.flexes} flex records/{report.flex_vertices} flex vertices, "
            f"{report.eyeballs} eyeballs, {report.mouths} phoneme mouths; "
            f"normalized {report.flex_type_fields_normalized} legacy flex-type field(s), "
            f"{len(report.patch_ranges)} descriptor ranges and "
            f"{report.hitbox_name_pointers_rebased} hitbox-name pointer(s)"
        )
        if report.sequence_anim_refs_outside_local_table:
            message += (
                f"; preserved {report.sequence_anim_refs_outside_local_table} "
                "sequence animation index(es) outside the local animation table"
            )
        if vtx_layout == "alien-swarm" and vtx_stats["files"]:
            message += (f"; upgraded {vtx_stats['files']} VTX file(s) to full Alien Swarm v49 "
                        f"layout ({vtx_stats['groups']} strip groups, {vtx_stats['strips']} strips, "
                        f"+{vtx_stats['expanded_bytes']} bytes)")
        if report.include_models:
            message += f"; references {len(report.include_models)} included model(s)"
        if experimental_v43:
            message += "; WARNING: v43 layout assumption is experimental"
        return ConversionResult(rel, "dry-run", message, external_ani=external_ani)

    patched = bytearray(original)
    struct.pack_into("<i", patched, 4, TARGET_VERSION)
    for offset, size, _label in report.patch_ranges:
        patched[offset:offset + size] = b"\0" * size
    for offset, replacement, _label in report.patch_values:
        patched[offset:offset + len(replacement)] = replacement

    # Post-translation validation uses the same parser against the target
    # interpretation and verifies all newly introduced fields are zero.
    struct.pack_into("<i", patched, 4, TARGET_VERSION)
    for offset, size, label in report.patch_ranges:
        if any(patched[offset:offset + size]):
            raise ConversionError(f"Internal normalization failed for {label}.")

    # Only the version word and the schema fields above are allowed to change.
    allowed = set(range(4, 8))
    for offset, size, _label in report.patch_ranges:
        allowed.update(range(offset, offset + size))
    for offset, replacement, _label in report.patch_values:
        allowed.update(range(offset, offset + len(replacement)))
    changed = [i for i, (a, b) in enumerate(zip(original, patched)) if a != b]
    unexpected = [i for i in changed if i not in allowed]
    if unexpected:
        raise ConversionError(f"Legacy writer altered an unapproved byte at 0x{unexpected[0]:X}.")

    # A second parse under the modern interpretation checks the same stable
    # structures and all local animation tracks after the legacy slots are zero.
    _validate_episode_one_target_v49(bytes(patched), source_path=source_mdl)

    output_mdl = output_root / rel
    output_mdl.parent.mkdir(parents=True, exist_ok=True)
    temp_mdl = output_mdl.with_name(output_mdl.stem + ".v49_tmp.mdl")
    try:
        temp_mdl.write_bytes(patched)
        out_version, out_checksum, out_length = read_mdl_header(temp_mdl)
        if (out_version, out_checksum, out_length) != (TARGET_VERSION, checksum, len(original)):
            raise ConversionError("Translated legacy MDL failed header verification.")
        if temp_mdl.read_bytes() != bytes(patched):
            raise ConversionError("Translated legacy MDL changed during disk verification.")
        replace_preserving_metadata(temp_mdl, output_mdl, source_mdl)
        _write_prepared_sidecars(prepared_vtx, full_v49=(vtx_layout == "alien-swarm"))
    finally:
        if temp_mdl.exists():
            temp_mdl.unlink()

    message = (
        f"translated legacy v{source_version} -> v49; {report.bones} bones, "
        f"{report.procedural_bones} procedural bones, {report.animations} animations, "
        f"{report.sequences} sequences, {report.ik_rules} IK rules, "
        f"{report.animation_tracks} compressed tracks; "
        f"preserved {report.flexes} flex records/{report.flex_vertices} flex vertices, "
        f"{report.eyeballs} eyeballs and {report.mouths} phoneme mouths; "
        f"normalized {report.flex_type_fields_normalized} legacy flex-type field(s), "
        f"{len(report.patch_ranges)} legacy schema ranges and "
        f"rebased {report.hitbox_name_pointers_rebased} hitbox-name pointer(s); "
        "vertex-delta payload, checksum and eye/flex references preserved"
    )
    if report.sequence_anim_refs_outside_local_table:
        message += (
            f"; preserved {report.sequence_anim_refs_outside_local_table} "
            "sequence animation index(es) outside the local animation table"
        )
    if vtx_layout == "alien-swarm" and vtx_stats["files"]:
        message += (f"; upgraded {vtx_stats['files']} VTX file(s) to full v49 layout "
                    f"({vtx_stats['groups']} strip groups, {vtx_stats['strips']} strips, "
                    f"+{vtx_stats['expanded_bytes']} bytes)")
    if report.include_models:
        message += "; external included models required: " + ", ".join(report.include_models)
    if experimental_v43:
        message += "; EXPERIMENTAL: v43 interpreted using Episode One v44 schema"
    if external_ani:
        message += "; external .ani copied unchanged (animation blocks not decoded)"
    return ConversionResult(rel, "converted", message, external_ani=external_ani)


def _validate_episode_one_target_v49(data: bytes, *, source_path: Path | None = None) -> None:
    if _i32(data, 4) != TARGET_VERSION:
        raise ConversionError("Target legacy validation did not receive a v49 file.")
    report = inspect_episode_one_v44(data, expected_version=TARGET_VERSION, source_path=source_path)
    # Every byte in the schema translation ranges must be zero in v49.
    for offset, size, label in report.patch_ranges:
        if any(data[offset:offset + size]):
            raise ConversionError(f"Target v49 field is not normalized: {label}.")

def paths_overlap(a: Path, b: Path) -> bool:
    """True if either directory is inside the other (including equality)."""
    a = a.resolve()
    b = b.resolve()
    try:
        b.relative_to(a)
        return True
    except ValueError:
        pass
    try:
        a.relative_to(b)
        return True
    except ValueError:
        return False


def progress(index: int, total: int, label: str) -> None:
    width = 28
    ratio = index / max(total, 1)
    filled = int(width * ratio)
    bar = "#" * filled + "-" * (width - filled)
    print(f"\r[{bar}] {index:>4}/{total:<4} {label[:55]:55}", end="", flush=True)
    if index >= total:
        print()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Convert Source Engine model sets to the Alien Swarm full-v49 MDL/VTX profile."
    )
    parser.add_argument("model_directory", type=Path, help="Directory containing .mdl files (searched recursively).")
    parser.add_argument("--output", type=Path, help="Output directory (default: <input>_mdl49).")
    parser.add_argument("--dry-run", action="store_true", help="Import, validate and report without writing files.")
    parser.add_argument(
        "--vtx-layout", choices=("alien-swarm", "legacy"), default="alien-swarm",
        help="Target VTX v7 layout: full Alien Swarm v49 (default), or legacy L4D2/HL2 layout."
    )
    parser.add_argument("--overwrite", action="store_true", help="Delete an existing output directory before conversion.")
    parser.add_argument(
        "--reject-external-ani", action="store_true",
        help="Skip models with a sibling .ani file instead of preserving it unchanged."
    )
    parser.add_argument(
        "--modern-v44", action="store_true",
        help="Force the later Orange Box/modern v44 layout (validated byte-preserving patch)."
    )
    parser.add_argument(
        "--assume-v43-episode-one", action="store_true",
        help="EXPERIMENTAL: parse v43 using the Episode One v44 layout; use only with independent format evidence."
    )
    parser.add_argument(
        "--preserve-unclaimed-bytes", action="store_true",
        help=("Allow up to 16 ValveMDL round-trip differences only when every differing byte is "
              "marked UNCLAIMED by ValveMDL; restore those exact source bytes in the output. "
              "STRUCT/OPAQUE changes, size changes, collisions and checksum mismatches still fail.")
    )
    parser.add_argument(
        "--max-unclaimed-differences", type=int, default=16,
        help="Maximum UNCLAIMED byte differences accepted with --preserve-unclaimed-bytes (default: 16)."
    )
    args = parser.parse_args(argv)
    if args.max_unclaimed_differences < 0:
        parser.error("--max-unclaimed-differences cannot be negative")

    input_root = args.model_directory.expanduser().resolve()
    if not input_root.is_dir():
        parser.error(f"model directory does not exist or is not a directory: {input_root}")

    output_root = (
        args.output.expanduser().resolve()
        if args.output else input_root.with_name(input_root.name + "_mdl49")
    )
    if paths_overlap(input_root, output_root):
        parser.error("input and output directories must not contain one another; choose a separate output directory")

    model_files = sorted(
        (p for p in input_root.rglob("*") if p.is_file() and p.suffix.lower() == ".mdl"),
        key=lambda p: str(p).lower(),
    )
    if not model_files:
        print(f"No .mdl files found under: {input_root}")
        return 2

    # All supported modern versions now have a byte-preserving conversion
    # path; ValveMDL is not needed for this batch.
    api = None

    print(f"Found {len(model_files)} MDL file(s) under: {input_root}")
    print(f"Target: MDL v49 + VTX v7 ({'full Alien Swarm' if args.vtx_layout == 'alien-swarm' else 'legacy'} layout).")
    print("modern v44-v48 mode: version-aware byte-preserving migration (no ValveMDL rebuild)")
    if args.modern_v44:
        print("v44 mode: forced modern layout")
    else:
        print("v44 mode: automatic (Episode One unless modern header markers are present)")
    if args.assume_v43_episode_one:
        print("WARNING: v43 is being tested as an experimental v44-layout candidate.")
    if args.preserve_unclaimed_bytes:
        print(
            "Relaxed round-trip mode: preserve only ValveMDL-UNCLAIMED bytes "
            f"(maximum {args.max_unclaimed_differences} per file); real checksum mismatches remain errors."
        )
    version_counts: dict[int, int] = {}
    for model_path in model_files:
        try:
            model_version, _checksum, _declared_length = read_mdl_header(model_path)
            version_counts[model_version] = version_counts.get(model_version, 0) + 1
        except Exception:
            version_counts[-1] = version_counts.get(-1, 0) + 1
    if version_counts:
        version_text = ", ".join(
            f"v{version}: {count}" if version >= 0 else f"invalid header: {count}"
            for version, count in sorted(version_counts.items())
        )
        print(f"Version distribution: {version_text}")
    if args.dry_run:
        print("Dry run: no files will be written.\n")
    else:
        if output_root.exists():
            if not args.overwrite:
                print(
                    f"ERROR: output directory already exists: {output_root}\n"
                    "Choose another --output path or pass --overwrite.",
                    file=sys.stderr,
                )
                return 2
            shutil.rmtree(output_root)
        print(f"Copying model directory to: {output_root}")
        shutil.copytree(input_root, output_root)

    results: list[ConversionResult] = []
    for number, source_mdl in enumerate(model_files, 1):
        progress(number - 1, len(model_files), source_mdl.relative_to(input_root).as_posix())
        try:
            result = convert_one(
                source_mdl, input_root, output_root, api, args.dry_run,
                args.reject_external_ani, modern_v44=args.modern_v44,
                assume_v43_episode_one=args.assume_v43_episode_one,
                preserve_unclaimed_bytes=args.preserve_unclaimed_bytes,
                max_unclaimed_differences=args.max_unclaimed_differences,
                vtx_layout=args.vtx_layout,
            )
        except Exception as exc:
            result = ConversionResult(
                source_mdl.relative_to(input_root), "failed", str(exc)
            )
        results.append(result)
        progress(number, len(model_files), f"{result.status}: {result.path}")

    converted_count = sum(r.status == "converted" for r in results)
    dry_count = sum(r.status == "dry-run" for r in results)
    already_count = sum(r.status == "already-v49" for r in results)
    failed = [r for r in results if r.status == "failed"]
    external_ani = [r for r in results if r.external_ani]

    print("\nResults")
    print("-------")
    if args.dry_run:
        print(f"Would convert: {dry_count}")
    else:
        print(f"Converted:     {converted_count}")
    print(f"Already v49:   {already_count}")
    print(f"Failed/skipped:{len(failed):4}")
    print(f"External ANI:  {len(external_ani)} (copied unchanged; not parsed by ValveMDL)")

    for result in results:
        if result.status == "converted" or result.status == "dry-run":
            print(f"[OK]   {result.path}: {result.message}")
        elif result.status == "already-v49":
            print(f"[SKIP] {result.path}: already v49")

    if failed:
        print("Failed:")
        with open("conversion_errors.txt","a") as convErrorList:
            for result in results:
                if result.status == "failed":
                    print(f"[FAIL] {result.path}: {result.message}")
                    convErrorList.write(f"[FAIL] {result.path}: {result.message}\n")

    if not args.dry_run:
        print(f"\nOutput: {output_root}")
    if failed:
        print(
            "\nSome models were not converted. They remain as their original copied files "
            "in the output directory; inspect the errors above.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
