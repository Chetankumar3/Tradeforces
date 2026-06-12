import { useRef, useState, type DragEvent } from 'react'
import { FolderOpen, File as FileIcon, UploadCloud } from 'lucide-react'
import { colors } from '../theme/colors'

interface FileDropzoneProps {
  // Mode A — a ready-to-upload .zip File.
  onZipSelected: (file: File) => void
  // Mode B — a list of files from a selected/dropped folder.
  onFolderSelected: (files: File[]) => void
  disabled?: boolean
}

export default function FileDropzone({
  onZipSelected,
  onFolderSelected,
  disabled,
}: FileDropzoneProps) {
  const zipInputRef = useRef<HTMLInputElement>(null)
  const folderInputRef = useRef<HTMLInputElement>(null)
  const [dragOver, setDragOver] = useState(false)

  const handleZipInput = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0]
    if (file) onZipSelected(file)
    e.target.value = ''
  }

  const handleFolderInput = (e: React.ChangeEvent<HTMLInputElement>) => {
    const files = e.target.files
    if (files && files.length > 0) onFolderSelected(Array.from(files))
    e.target.value = ''
  }

  // Recursively walk a dropped directory entry into a flat File[] list,
  // preserving webkitRelativePath-style paths via `fullPath`.
  const readEntry = (entry: any, path: string): Promise<File[]> =>
    new Promise((resolve) => {
      if (entry.isFile) {
        entry.file((file: File) => {
          const rel = (path ? `${path}/` : '') + file.name
          Object.defineProperty(file, 'webkitRelativePath', {
            value: rel,
            writable: false,
          })
          resolve([file])
        })
      } else if (entry.isDirectory) {
        const reader = entry.createReader()
        reader.readEntries(async (entries: any[]) => {
          const nested = await Promise.all(
            entries.map((child) =>
              readEntry(child, `${path ? `${path}/` : ''}${entry.name}`),
            ),
          )
          resolve(nested.flat())
        })
      } else {
        resolve([])
      }
    })

  const handleDrop = async (e: DragEvent<HTMLDivElement>) => {
    e.preventDefault()
    setDragOver(false)
    if (disabled) return

    const items = e.dataTransfer.items
    if (items && items.length > 0) {
      const firstEntry = items[0].webkitGetAsEntry?.()
      // Mode B — dropped a folder.
      if (firstEntry?.isDirectory) {
        const all: File[] = []
        for (let i = 0; i < items.length; i++) {
          const entry = items[i].webkitGetAsEntry?.()
          if (entry) all.push(...(await readEntry(entry, '')))
        }
        if (all.length > 0) onFolderSelected(all)
        return
      }
    }

    // Mode A — dropped a .zip file.
    const file = e.dataTransfer.files?.[0]
    if (file && file.name.toLowerCase().endsWith('.zip')) {
      onZipSelected(file)
    }
  }

  return (
    <div
      onDragOver={(e) => {
        e.preventDefault()
        if (!disabled) setDragOver(true)
      }}
      onDragLeave={() => setDragOver(false)}
      onDrop={handleDrop}
      className="flex flex-col items-center justify-center gap-5 rounded-xl border-2 border-dashed px-6 py-12 text-center transition-colors duration-150"
      style={{
        background: colors.bg.overlay,
        borderColor: dragOver ? colors.accent.light : colors.border.default,
        opacity: disabled ? 0.6 : 1,
      }}
    >
      <UploadCloud size={48} style={{ color: colors.text.secondary }} />
      <div>
        <p className="font-medium" style={{ color: colors.text.primary }}>
          Drag &amp; drop a folder or .zip file here
        </p>
        <p className="mt-1 text-sm" style={{ color: colors.text.secondary }}>
          or choose one below
        </p>
      </div>

      <div className="flex flex-wrap items-center justify-center gap-3">
        <button
          type="button"
          disabled={disabled}
          onClick={() => folderInputRef.current?.click()}
          className="flex items-center gap-2 rounded-lg px-4 py-2.5 font-medium transition-colors duration-150 disabled:opacity-60"
          style={{ background: colors.accent.primary, color: colors.text.inverse }}
          onMouseEnter={(e) =>
            !disabled && (e.currentTarget.style.background = colors.accent.hover)
          }
          onMouseLeave={(e) =>
            (e.currentTarget.style.background = colors.accent.primary)
          }
        >
          <FolderOpen size={18} />
          Select Folder
        </button>

        <button
          type="button"
          disabled={disabled}
          onClick={() => zipInputRef.current?.click()}
          className="flex items-center gap-2 rounded-lg border px-4 py-2.5 font-medium transition-colors duration-150 disabled:opacity-60"
          style={{ borderColor: colors.border.default, color: colors.text.primary }}
          onMouseEnter={(e) =>
            !disabled && (e.currentTarget.style.background = colors.bg.tertiary)
          }
          onMouseLeave={(e) => (e.currentTarget.style.background = 'transparent')}
        >
          <FileIcon size={18} />
          Select .zip File
        </button>
      </div>

      {/* Hidden inputs */}
      <input
        ref={zipInputRef}
        type="file"
        accept=".zip"
        className="hidden"
        onChange={handleZipInput}
      />
      <input
        ref={folderInputRef}
        type="file"
        // @ts-expect-error — non-standard but widely supported directory picker attrs
        webkitdirectory=""
        directory=""
        multiple
        className="hidden"
        onChange={handleFolderInput}
      />
    </div>
  )
}
