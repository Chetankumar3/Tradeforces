import { useState, type FormEvent } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { useMutation } from '@tanstack/react-query'
import { UserPlus, Loader2, AlertCircle, BarChart2 } from 'lucide-react'
import { register as registerRequest } from '../api/auth'
import { useAuth } from '../contexts/AuthContext'
import { getErrorMessage } from '../utils/apiError'
import TextField from '../components/TextField'
import { colors } from '../theme/colors'

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/

export default function RegisterPage() {
  const navigate = useNavigate()
  const { login } = useAuth()

  const [username, setUsername] = useState('')
  const [name, setName] = useState('')
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [confirm, setConfirm] = useState('')
  const [touched, setTouched] = useState(false)

  const errors = {
    username: !username.trim() ? 'Username is required' : '',
    name: !name.trim() ? 'Full name is required' : '',
    email: !email.trim()
      ? 'Email is required'
      : !EMAIL_RE.test(email.trim())
        ? 'Enter a valid email address'
        : '',
    password: !password
      ? 'Password is required'
      : password.length < 6
        ? 'Password must be at least 6 characters'
        : '',
    confirm: !confirm
      ? 'Please confirm your password'
      : confirm !== password
        ? 'Passwords do not match'
        : '',
  }

  const hasErrors = Object.values(errors).some(Boolean)

  const mutation = useMutation({
    mutationFn: () =>
      registerRequest({
        username: username.trim(),
        name: name.trim(),
        email: email.trim(),
        password,
      }),
    onSuccess: (data) => {
      login(data.access_token, data.user_id)
      navigate('/dashboard', { replace: true })
    },
  })

  const handleSubmit = (e: FormEvent) => {
    e.preventDefault()
    setTouched(true)
    if (hasErrors) return
    mutation.mutate()
  }

  const show = (key: keyof typeof errors) => (touched ? errors[key] : '')

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
          Create your account
        </h1>
        <p className="mb-6 text-sm" style={{ color: colors.text.secondary }}>
          Register your team to start submitting algorithms.
        </p>

        {mutation.isError && (
          <div
            className="mb-4 flex items-center gap-2 rounded-lg border px-3 py-2 text-sm"
            style={{ borderColor: colors.status.error, color: colors.text.primary }}
          >
            <AlertCircle size={18} style={{ color: colors.status.error }} />
            {getErrorMessage(mutation.error, 'Registration failed')}
          </div>
        )}

        <form onSubmit={handleSubmit} className="flex flex-col gap-4">
          <TextField
            label="Username"
            value={username}
            onChange={setUsername}
            error={show('username')}
            autoComplete="username"
            autoFocus
          />
          <TextField
            label="Full name"
            value={name}
            onChange={setName}
            error={show('name')}
            autoComplete="name"
          />
          <TextField
            label="Email"
            type="email"
            value={email}
            onChange={setEmail}
            error={show('email')}
            autoComplete="email"
          />
          <TextField
            label="Password"
            type="password"
            value={password}
            onChange={setPassword}
            error={show('password')}
            autoComplete="new-password"
          />
          <TextField
            label="Confirm password"
            type="password"
            value={confirm}
            onChange={setConfirm}
            error={show('confirm')}
            autoComplete="new-password"
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
              <UserPlus size={18} />
            )}
            {mutation.isPending ? 'Creating account…' : 'Create account'}
          </button>
        </form>

        <p className="mt-6 text-center text-sm" style={{ color: colors.text.secondary }}>
          Already have an account?{' '}
          <Link
            to="/login"
            className="font-medium hover:underline"
            style={{ color: colors.text.accent }}
          >
            Login →
          </Link>
        </p>
      </div>
    </div>
  )
}
