import trimesh
from cadquery import importers
import os

# Configuration
folder = "C:/Users/jhon/Desktop/case/step/"
output_folder = "C:/Users/jhon/Desktop/case/stl/"

# Create output folder if it doesn't exist
os.makedirs(output_folder, exist_ok=True)

# List of STEP files
step_files = [
    "case_maixcam.step",
    "maix_bottom_case.step",
    "maix_top_case.step",
    "MTF-02P_bottom_case.step",
    "MTF-02P_casev2.step",
    "MTF-02P_top_case.step"
]

# Conversion parameters
TOLERANCE = 0.001        # Adjust as needed (0.001 = high quality)
ANGULAR_TOLERANCE = 0.1

print(f"Starting conversion of {len(step_files)} files...\n")

# Process each file
results = []
for i, file in enumerate(step_files, 1):
    input_path = os.path.join(folder, file)
    output_name = file.replace('.step', '.stl')
    output_path = os.path.join(output_folder, output_name)
    
    print(f"[{i}/{len(step_files)}] Processing: {file}")
    
    try:
        # Verify file exists
        if not os.path.exists(input_path):
            print(f"  ✗ File not found: {input_path}\n")
            results.append((file, "Not found", None))
            continue
        
        # Import STEP
        result = importers.importStep(input_path)
        
        # Export to STL
        result.val().exportStl(output_path, 
                               tolerance=TOLERANCE,
                               angularTolerance=ANGULAR_TOLERANCE)
        
        # Analyze mesh
        mesh = trimesh.load(output_path)
        
        # Display info
        print(f"  ✓ Conversion successful")
        print(f"    Volume: {mesh.volume:.2f} mm³")
        print(f"    Area: {mesh.area:.2f} mm²")
        print(f"    Faces: {len(mesh.faces):,}")
        print(f"    Watertight: {'Yes' if mesh.is_watertight else 'No'}")
        print(f"    Saved: {output_name}\n")
        
        results.append((file, "Success", mesh))
        
    except Exception as e:
        print(f"  ✗ Error: {str(e)}\n")
        results.append((file, f"Error: {str(e)}", None))

# Final summary
print("="*60)
print("CONVERSION SUMMARY")
print("="*60)
successful = sum(1 for _, status, _ in results if status == "Success")
print(f"\nTotal files: {len(step_files)}")
print(f"Successful: {successful}")
print(f"Failed: {len(step_files) - successful}")
print(f"\nSTL files saved in: {output_folder}")

# Detail of converted files
print("\nConverted files:")
for file, status, mesh in results:
    if status == "Success":
        print(f"  ✓ {file.replace('.step', '.stl')}")
    else:
        print(f"  ✗ {file} - {status}")