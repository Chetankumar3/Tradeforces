import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { ChevronRight, Upload, File as FileIcon, X } from 'lucide-react'
import Navbar from '../components/Navbar'
import FileDropzone from '../components/FileDropzone'
import UploadProgress from '../components/UploadProgress'
import { zipFolder } from '../utils/zipFolder'
import { formatBytes } from '../utils/formatBytes'
import { getErrorMessage } from '../utils/apiError'
import {
  getPresignedUrl,
  uploadToGCS,
  notifyUploadComplete,
} from '../api/submissions'
import type { UploadStatus } from '../types/upload'
import { colors } from '../theme/colors'

export default function UploadPage() {
  const navigate = useNavigate()

  const [file, setFile] = useState<File | null>(null)
  const [status, setStatus] = useState<UploadStatus>('idle')
  const [zipProgress, setZipProgress] = useState(0)
  const [uploadProgress, setUploadProgress] = useState(0)
  const [submissionId, setSubmissionId] = useState<number | null>(null)
  const [errorMessage, setErrorMessage] = useState('')

  const busy =
    status === 'zipping' ||
    status === 'requesting' ||
    status === 'uploading' ||
    status === 'notifying'

  const handleZipSelected = (zip: File) => {
    setFile(zip)
    setStatus('idle')
    setErrorMessage('')
  }

  const handleFolderSelected = async (files: File[]) => {
    setErrorMessage('')
    setZipProgress(0)
    setStatus('zipping')
    try {
      const zip = await zipFolder(files, setZipProgress)
      setFile(zip)
      setStatus('idle')
    } catch (err) {
      setErrorMessage(getErrorMessage(err, 'Failed to compress folder'))
      setStatus('error')
    }
  }

  const runUpload = async (target: File) => {
    setErrorMessage('')
    try {
      // Step 1 — request presigned URL.
      setStatus('requesting')
      const clientId = Date.now()
      const { presigned_url, submission_id } = await getPresignedUrl(clientId)

      // Step 2 — upload directly to GCS.
      setStatus('uploading')
      setUploadProgress(0)
      await uploadToGCS(presigned_url, target, setUploadProgress)

      // Step 3 — notify backend.
      setStatus('notifying')
      await notifyUploadComplete(submission_id)

      setSubmissionId(submission_id)
      setStatus('done')

      navigate('/dashboard', { state: { lastSubmissionId: submission_id } })
    } catch (err) {
      setErrorMessage(getErrorMessage(err, 'Upload failed'))
      setStatus('error')
    }
  }

  const handleUpload = () => {
    if (file) runUpload(file)
  }

  const handleRetry = () => {
    if (file) runUpload(file)
  }

  const clearFile = () => {
    setFile(null)
    setStatus('idle')
    setErrorMessage('')
    setUploadProgress(0)
    setZipProgress(0)
  }

  const showDropzone = status === 'idle' || status === 'zipping'

  return (
    <div className="flex min-h-full flex-col" style={{ background: colors.bg.primary }}>
      <Navbar />

      <main className="mx-auto w-full max-w-2xl flex-1 px-6 py-8">
        <button
          onClick={() => navigate('/dashboard')}
          className="mb-6 flex items-center gap-1 text-sm transition-colors duration-150"
          style={{ color: colors.text.secondary }}
          onMouseEnter={(e) => (e.currentTarget.style.color = colors.text.accent)}
          onMouseLeave={(e) => (e.currentTarget.style.color = colors.text.secondary)}
        >
          <ChevronRight size={18} style={{ transform: 'rotate(180deg)' }} />
          Back to dashboard
        </button>

        <h1 className="mb-1 text-2xl font-bold" style={{ color: colors.text.primary }}>
          New Submission
        </h1>
        <p className="mb-6 text-sm" style={{ color: colors.text.secondary }}>
          Upload your trading algorithm as a folder or a .zip archive.
        </p>

        {showDropzone && (
          <FileDropzone
            onZipSelected={handleZipSelected}
            onFolderSelected={handleFolderSelected}
            disabled={status === 'zipping'}
          />
        )}

        {/* Selected file info */}
        {file && (status === 'idle' || status === 'error') && (
          <div
            className="mt-4 flex items-center gap-3 rounded-xl border p-4"
            style={{
              background: colors.bg.secondary,
              borderColor: colors.border.default,
            }}
          >
            <div
              className="flex h-10 w-10 items-center justify-center rounded-lg"
              style={{ background: colors.accent.subtle }}
            >
              <FileIcon size={20} style={{ color: colors.accent.light }} />
            </div>
            <div className="min-w-0 flex-1">
              <div
                className="truncate font-medium"
                style={{ color: colors.text.primary }}
              >
                {file.name}
              </div>
              <div className="text-sm font-mono" style={{ color: colors.mono }}>
                {formatBytes(file.size)}
              </div>
            </div>
            <button
              onClick={clearFile}
              className="rounded-lg p-1.5 transition-colors duration-150"
              style={{ color: colors.text.secondary }}
              onMouseEnter={(e) => (e.currentTarget.style.background = colors.bg.tertiary)}
              onMouseLeave={(e) => (e.currentTarget.style.background = 'transparent')}
              aria-label="Remove file"
            >
              <X size={18} />
            </button>
          </div>
        )}

        {/* Status + progress */}
        {status !== 'idle' && (
          <div className="mt-4">
            <UploadProgress
              status={status}
              zipProgress={zipProgress}
              uploadProgress={uploadProgress}
              submissionId={submissionId}
              errorMessage={errorMessage}
              onRetry={handleRetry}
            />
          </div>
        )}

        {/* Upload button */}
        <button
          onClick={handleUpload}
          disabled={!file || busy}
          className="mt-6 flex w-full items-center justify-center gap-2 rounded-lg px-4 py-3 font-medium transition-colors duration-150 disabled:cursor-not-allowed disabled:opacity-50"
          style={{ background: colors.accent.primary, color: colors.text.inverse }}
          onMouseEnter={(e) =>
            !(!file || busy) && (e.currentTarget.style.background = colors.accent.hover)
          }
          onMouseLeave={(e) =>
            (e.currentTarget.style.background = colors.accent.primary)
          }
        >
          <Upload size={18} />
          {busy ? 'Working…' : 'Upload submission'}
        </button>
      </main>
    </div>
  )
}
