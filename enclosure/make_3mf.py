#!/usr/bin/env python3
"""Export the FYXIS enclosure for printing.

Renders each printed piece and its colour inlays with OpenSCAD, then packs them into one 3MF per
piece: a single object whose parts are the body and its inlays, already lined up, with a filament slot
assigned to each part (Bambu Studio reads this; other slicers keep the parts and let you pick).

    python3 make_3mf.py            # writes stl/fyxis_tray.stl, stl/fyxis_cap.3mf, stl/fyxis_keycap.3mf

Filament slots: 1 = body (black), 2 = white inlays, 3 = red. Change SLOTS below to match your AMS.
"""
import os
import struct
import subprocess
import sys
import tempfile
import uuid
import zipfile
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
SCAD = os.path.join(HERE, "bigsix_enclosure.scad")
OUT = os.path.join(HERE, "stl")
OPENSCAD = os.environ.get("OPENSCAD", "openscad")

SLOTS = {"body": 1, "white": 2, "red": 3}
APP = "BambuStudio-02.05.00.66"   # format marker: Bambu Studio only reads per-part settings from files it recognises

# printed piece -> parts as (scad part, part name, colour slot); the first part is the body
PIECES = {
    "fyxis_cap": [("cap", "cap", SLOTS["body"]),
                  ("cap_inlay_white", "FYXIS + USB (white)", SLOTS["white"]),
                  ("cap_inlay_red", "key ring (red)", SLOTS["red"])],
    "fyxis_keycap": [("keycap", "key (red)", SLOTS["red"]),
                     ("keycap_inlay", "REC (white)", SLOTS["white"])],
}
SINGLE = {"fyxis_tray": "tray"}


def render(part, path):
    cmd = [OPENSCAD, "--backend=manifold", "--export-format=binstl", "-D", f'part="{part}"', "-o", path, SCAD]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(path):
        sys.exit(f"OpenSCAD failed on {part}:\n{r.stderr}")


def read_stl(path):
    """Binary STL -> (vertices, triangles) with shared vertices."""
    with open(path, "rb") as f:
        data = f.read()
    n = struct.unpack_from("<I", data, 80)[0]
    index, verts, tris = {}, [], []
    for i in range(n):
        o = 84 + i * 50 + 12
        tri = []
        for k in range(3):
            v = struct.unpack_from("<fff", data, o + k * 12)
            key = (round(v[0], 5), round(v[1], 5), round(v[2], 5))
            if key not in index:
                index[key] = len(verts)
                verts.append(key)
            tri.append(index[key])
        if len(set(tri)) == 3:
            tris.append(tri)
    return verts, tris


def mesh_xml(obj_id, verts, tris):
    v = "".join(f'<vertex x="{x:.5f}" y="{y:.5f}" z="{z:.5f}"/>' for x, y, z in verts)
    t = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris)
    return (f'<object id="{obj_id}" p:UUID="{uuid.uuid4()}" type="model">'
            f"<mesh><vertices>{v}</vertices><triangles>{t}</triangles></mesh></object>")


def write_3mf(path, name, parts):
    """parts: list of (part name, slot, verts, tris). One object, one part per mesh."""
    # Same namespaces and markers Bambu Studio writes, so it reads the parts and filament slots below
    # instead of loading geometry only.
    ns = ('xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" '
          'xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" '
          'xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p"')
    bbl = '<metadata name="BambuStudio:3mfVersion">1</metadata>'
    sub_path = "/3D/Objects/object_1.model"
    sub = (f'<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" {ns}>'
           + bbl + "<resources>" + "".join(mesh_xml(i + 1, v, t) for i, (_, _, v, t) in enumerate(parts)) +
           "</resources><build/></model>")
    parent = len(parts) + 1
    # centre the object on a 256 mm plate (all parts share one frame, so they move together)
    allv = [v for _, _, verts, _ in parts for v in verts]
    cx = 128 - (min(v[0] for v in allv) + max(v[0] for v in allv)) / 2
    cy = 128 - (min(v[1] for v in allv) + max(v[1] for v in allv)) / 2
    place = f"1 0 0 0 1 0 0 0 1 {cx:.4f} {cy:.4f} 0"
    comps = "".join(f'<component p:path="{sub_path}" objectid="{i + 1}" p:UUID="{uuid.uuid4()}" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>'
                    for i in range(len(parts)))
    main = (f'<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" {ns}>'
            f'<metadata name="Application">{APP}</metadata>' + bbl +
            f'<metadata name="Title">{escape(name)}</metadata>'
            f'<resources><object id="{parent}" p:UUID="{uuid.uuid4()}" type="model"><components>{comps}</components></object></resources>'
            f'<build p:UUID="{uuid.uuid4()}"><item objectid="{parent}" p:UUID="{uuid.uuid4()}" '
            f'transform="{place}" printable="1"/></build></model>')
    cfg_parts = "".join(
        f'  <part id="{i + 1}" subtype="normal_part">\n'
        f'    <metadata key="name" value="{escape(pname)}"/>\n'
        f'    <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n'
        f'    <metadata key="extruder" value="{slot}"/>\n  </part>\n'
        for i, (pname, slot, _, _) in enumerate(parts))
    config = (f'<?xml version="1.0" encoding="UTF-8"?>\n<config>\n <object id="{parent}">\n'
              f'  <metadata key="name" value="{escape(name)}"/>\n  <metadata key="extruder" value="{parts[0][1]}"/>\n'
              f"{cfg_parts} </object>\n"
              # plate and assembly entries, as Bambu Studio writes them
              f' <plate>\n  <metadata key="plater_id" value="1"/>\n  <metadata key="plater_name" value=""/>\n'
              f'  <metadata key="locked" value="false"/>\n  <model_instance>\n'
              f'   <metadata key="object_id" value="{parent}"/>\n   <metadata key="instance_id" value="0"/>\n'
              f'   <metadata key="identify_id" value="{parent * 100 + 1}"/>\n  </model_instance>\n </plate>\n'
              f' <assemble>\n  <assemble_item object_id="{parent}" instance_id="0" transform="{place}" offset="0 0 0"/>\n'
              f' </assemble>\n</config>\n')
    content_types = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                     '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
                     '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
                     '<Default Extension="config" ContentType="text/xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>'
            '</Relationships>')
    model_rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                  f'<Relationship Target="{sub_path}" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>'
                  '</Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", main)
        z.writestr("3D/_rels/3dmodel.model.rels", model_rels)
        z.writestr("3D/Objects/object_1.model", sub)
        z.writestr("Metadata/model_settings.config", config)


def main():
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, part in SINGLE.items():
            render(part, os.path.join(OUT, name + ".stl"))
            print(f"stl/{name}.stl")
        for name, spec in PIECES.items():
            parts = []
            for scad_part, pname, slot in spec:
                f = os.path.join(tmp, scad_part + ".stl")
                render(scad_part, f)
                v, t = read_stl(f)
                parts.append((pname, slot, v, t))
            write_3mf(os.path.join(OUT, name + ".3mf"), name, parts)
            print(f"stl/{name}.3mf  (" + ", ".join(f"{p[0]} -> slot {p[1]}" for p in parts) + ")")


if __name__ == "__main__":
    main()
