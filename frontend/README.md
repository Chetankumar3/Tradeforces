# Tradeforces — Frontend

Single-page app for the Tradeforces high-frequency trading hackathon platform.
Teams register, log in, and submit trading algorithms (folder or `.zip`) which are
zipped client-side and uploaded to GCS via a presigned URL.

## Stack
React 18 · TypeScript · Vite · React Router v6 · TanStack Query · Axios · Tailwind CSS · Lucide React · JSZip

## Setup
```bash
npm install
cp .env.example .env   # set VITE_MAIN_API_URL
npm run dev            # http://localhost:5173
```

## Environment
| Variable            | Description                |
|---------------------|----------------------------|
| `VITE_MAIN_API_URL` | Base URL of the main API.  |

## Scripts
- `npm run dev` — start the dev server
- `npm run build` — type-check (`tsc -b`) + production build
- `npm run preview` — preview the production build

## Theming
All colors live in **`src/theme/colors.ts`** — the single source of truth.
Edit that file to retheme the entire app. No hex values are hardcoded elsewhere.

## Structure
```
src/
  theme/colors.ts            palette (edit to retheme)
  contexts/AuthContext.tsx   JWT auth state + localStorage persistence
  api/
    client.ts                axios instance, auth interceptor, 401 handler
    auth.ts                  register(), login()
    submissions.ts           getPresignedUrl(), uploadToGCS(), notifyUploadComplete()
  utils/
    zipFolder.ts             JSZip folder -> File
    formatBytes.ts           byte formatter
    apiError.ts              extracts API error messages
  pages/                     Login, Register, Dashboard, Upload
  components/                ProtectedRoute, Navbar, FileDropzone,
                             UploadProgress, StatusBanner, TextField
```

## Upload pipeline
1. `GET /submit/{clientId}` → `{ presigned_url, submission_id }`
2. `PUT presigned_url` (file body, `Content-Type: application/zip`, **no auth header**)
3. `POST /upload_complete/{submission_id}`

Status states: `idle → zipping → requesting → uploading → notifying → done | error`.
A folder selection is compressed with JSZip first; a `.zip` skips straight to step 1.

> Submission history / status polling endpoints don't exist yet — the dashboard
> shows a "coming soon" placeholder.
