#!/usr/bin/env python3
"""
Image processing script to resize images from src/original_assets
- Resize to width=1920px (maintaining aspect ratio)
- Convert to JPEG format
- Remove EXIF metadata (including GPS data)
- Save to src/assets directory
"""

import sys
from PIL import Image
import pathlib

def process_image(input_path, output_path, target_width=1920):
    """
    Process a single image: resize, convert to JPEG, remove metadata
    """
    try:
        # Open image
        with Image.open(input_path) as img:
            print(f"Processing: {input_path}")
            print(f"  Original size: {img.size}")
            
            # Convert to RGB if necessary (for JPEG output)
            if img.mode in ('RGBA', 'LA', 'P'):
                img = img.convert('RGB')
            
            # Resize if width is larger than target
            if img.width > target_width:
                # Calculate new height maintaining aspect ratio
                aspect_ratio = img.height / img.width
                new_height = int(target_width * aspect_ratio)
                img = img.resize((target_width, new_height), Image.Resampling.LANCZOS)
                print(f"  Resized to: {img.size}")
            else:
                print(f"  No resize needed (width <= {target_width})")
            
            # Save without EXIF data (removes GPS and other metadata)
            img.save(output_path, 'JPEG', quality=85, optimize=True, exif=b'')
            print(f"  Saved to: {output_path}")
            
    except Exception as e:
        print(f"Error processing {input_path}: {e}")
        return False
    
    return True

def main():
    # Define paths
    script_dir = pathlib.Path(__file__).parent
    original_assets_dir = script_dir / "src" / "original_assets"
    assets_dir = script_dir / "src" / "assets"
    
    print(f"Source directory: {original_assets_dir}")
    print(f"Output directory: {assets_dir}")
    
    # Check if original_assets directory exists
    if not original_assets_dir.exists():
        print(f"Error: {original_assets_dir} does not exist!")
        sys.exit(1)
    
    # Create assets directory if it doesn't exist
    assets_dir.mkdir(exist_ok=True)
    
    # Supported image extensions
    supported_extensions = {'.jpg', '.jpeg', '.png', '.gif', '.bmp', '.tiff', '.webp'}
    
    # Process all images in original_assets
    processed_count = 0
    total_count = 0
    
    for file_path in original_assets_dir.iterdir():
        if file_path.is_file() and file_path.suffix.lower() in supported_extensions:
            total_count += 1
            
            # Create output filename (always .jpeg)
            output_name = file_path.stem + ".jpeg"
            output_path = assets_dir / output_name
            
            # Process the image
            if process_image(file_path, output_path):
                processed_count += 1
            
            print()  # Empty line for readability
    
    print(f"Processing complete!")
    print(f"Successfully processed: {processed_count}/{total_count} images")
    
    if processed_count < total_count:
        print(f"Failed to process: {total_count - processed_count} images")
        sys.exit(1)

if __name__ == "__main__":
    main()