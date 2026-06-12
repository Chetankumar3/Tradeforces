import JSZip from 'jszip'

// Derive the top-level folder name from a FileList produced by a
// `webkitdirectory` input. Falls back to a sensible default.
function deriveFolderName(files: FileList | File[]): string {
  const first = files[0]
  const rel = (first as File & { webkitRelativePath?: string })?.webkitRelativePath
  if (rel && rel.includes('/')) {
    return rel.split('/')[0]
  }
  return 'submission'
}

// JSZip wrapper: compress a selected folder (FileList) into a single .zip File.
// `onProgress` receives an integer percentage (0–100) during generation.
export async function zipFolder(
  files: FileList | File[],
  onProgress?: (percent: number) => void,
): Promise<File> {
  const zip = new JSZip()
  const list = Array.from(files)

  for (const file of list) {
    const path =
      (file as File & { webkitRelativePath?: string }).webkitRelativePath || file.name
    zip.file(path, file)
  }

  const blob = await zip.generateAsync(
    {
      type: 'blob',
      compression: 'DEFLATE',
      compressionOptions: { level: 6 },
    },
    (metadata) => {
      onProgress?.(Math.round(metadata.percent))
    },
  )

  const folderName = deriveFolderName(files)
  return new File([blob], `${folderName}.zip`, { type: 'application/zip' })
}
