import bpy, os, glob

bpy.ops.import_scene.fbx(filepath="sonic_dump/chr_Sonic_HD.model.fbx")
armature = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')

if not armature.animation_data:
    armature.animation_data_create()

for fbx in glob.glob("sonic_dump/anims/*.fbx"):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=fbx)
    new_objs = set(bpy.data.objects) - before

    for obj in new_objs:
        if obj.type == 'ARMATURE' and obj.animation_data and obj.animation_data.action:
            action = obj.animation_data.action
            action.name = os.path.splitext(os.path.basename(fbx))[0]
            action.use_fake_user = True

            # push onto MAIN armature's NLA stack as a strip
            track = armature.animation_data.nla_tracks.new()
            track.name = action.name
            track.strips.new(action.name, int(action.frame_range[0]), action)

        bpy.data.objects.remove(obj, do_unlink=True)
print("exporting")
bpy.ops.export_scene.gltf(
    filepath="sonic_dump/chr_Sonic_HD.gltf",
    export_format='GLTF_SEPARATE',
    export_animations=True,
    export_anim_single_armature=True,
    export_nla_strips=True,
)
print("finished")
