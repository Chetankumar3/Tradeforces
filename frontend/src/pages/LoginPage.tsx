import { useState, type FormEvent } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { useMutation } from '@tanstack/react-query'
import { LogIn, Loader2, AlertCircle, BarChart2 } from 'lucide-react'
import { login as loginRequest } from '../api/auth'
import { useAuth } from '../contexts/AuthContext'
import { getErrorMessage } from '../utils/apiError'
import TextField from '../components/TextField'
import { colors } from '../theme/colors'

export default function LoginPage() {
  const navigate = useNavigate()
  const { login } = useAuth()

  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [touched, setTouched] = useState(false)

  const usernameError = touched && !username.trim() ? 'Username is required' : ''
  const passwordError = touched && !password ? 'Password is required' : ''

  const mutation = useMutation({
    mutationFn: () => loginRequest({ username: username.trim(), password }),
    onSuccess: (data) => {
      login(data.access_token)
      navigate('/dashboard', { replace: true })
    },
  })

  const handleSubmit = (e: FormEvent) => {
    e.preventDefault()
    setTouched(true)
    if (!username.trim() || !password) return
    mutation.mutate()
  }

  return (
    <div
      className="flex min-h-full items-center justify-center px-4 py-12"
      style={{ background: colors.bg.primary }}
    >
      <div
        className="w-full max-w-md rounded-xl border p-8 shadow-lg"
        style={{ background: colors.bg.secondary, borderColor: colors.border.default }}
      >
        <div className="mb-8 flex items-center gap-2">
          <BarChart2 size={20} style={{ color: colors.accent.light }} />
          <span className="text-xl font-semibold" style={{ color: colors.text.primary }}>
            Tradeforces
          </span>
        </div>

        <h1 className="mb-1 text-2xl font-bold" style={{ color: colors.text.primary }}>
          Welcome back
        </h1>
        <p className="mb-6 text-sm" style={{ color: colors.text.secondary }}>
          Sign in to submit your trading algorithm.
        </p>

        {mutation.isError && (
          <div
            className="mb-4 flex items-center gap-2 rounded-lg border px-3 py-2 text-sm"
            style={{ borderColor: colors.status.error, color: colors.text.primary }}
          >
            <AlertCircle size={18} style={{ color: colors.status.error }} />
            {getErrorMessage(mutation.error, 'Invalid username or password')}
          </div>
        )}

        <form onSubmit={handleSubmit} className="flex flex-col gap-4">
          <TextField
            label="Username"
            value={username}
            onChange={setUsername}
            error={usernameError}
            autoComplete="username"
            autoFocus
          />
          <TextField
            label="Password"
            type="password"
            value={password}
            onChange={setPassword}
            error={passwordError}
            autoComplete="current-password"
          />

          <button
            type="submit"
            disabled={mutation.isPending}
            className="mt-2 flex items-center justify-center gap-2 rounded-lg px-4 py-2.5 font-medium transition-colors duration-150 disabled:opacity-60"
            style={{ background: colors.accent.primary, color: colors.text.inverse }}
            onMouseEnter={(e) =>
              !mutation.isPending &&
              (e.currentTarget.style.background = colors.accent.hover)
            }
            onMouseLeave={(e) =>
              (e.currentTarget.style.background = colors.accent.primary)
            }
          >
            {mutation.isPending ? (
              <Loader2 size={18} className="animate-spin" />
            ) : (
              <LogIn size={18} />
            )}
            {mutation.isPending ? 'Signing in…' : 'Sign in'}
          </button>
        </form>

        <p className="mt-6 text-center text-sm" style={{ color: colors.text.secondary }}>
          Don&apos;t have an account?{' '}
          <Link
            to="/register"
            className="font-medium hover:underline"
            style={{ color: colors.text.accent }}
          >
            Register →
          </Link>
        </p>
      </div>
    </div>
  )
}
