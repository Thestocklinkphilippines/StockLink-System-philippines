import React, { useEffect, useMemo, useState } from 'react'
import api from '../api'
import { getPollingIntervalMs } from '../pollingConfig'
import '../styles/admin-users.css'

export default function AdminUsers({ currentUser = null }) {
  const pollingIntervalMs = getPollingIntervalMs()
  const [users, setUsers] = useState([])
  const [loading, setLoading] = useState(true)
  const [message, setMessage] = useState('')
  const [error, setError] = useState('')
  const [busyUserId, setBusyUserId] = useState(null)

  const currentUserId = currentUser && Number.isFinite(Number(currentUser.id)) ? Number(currentUser.id) : null

  const adminCount = useMemo(() => users.filter(user => user.is_staff).length, [users])

  function getApprovalState(user) {
    if (user.approval && user.approval.is_approved) return 'approved'
    return 'pending'
  }

  function getRelevantVoteSummary(user) {
    const summary = user.role_vote || {}
    const voteCount = user.is_staff ? (summary.demote_votes || 0) : (summary.promote_votes || 0)
    return {
      voteCount,
      threshold: summary.threshold || 1,
      activeAdminCount: summary.active_admin_count || 0,
      voteType: user.is_staff ? 'demote' : 'promote',
    }
  }

  async function refreshUsers() {
    setError('')
    const res = await api.getJSON('/api/admin/users/')
    if (res.ok && Array.isArray(res.body)) {
      setUsers(res.body)
      return true
    }

    const detail = res.body && (res.body.detail || JSON.stringify(res.body))
    setError(detail || 'Failed to load users.')
    return false
  }

  useEffect(() => {
    let isMounted = true

    ;(async () => {
      setLoading(true)
      setError('')
      const ok = await refreshUsers()
      if (isMounted) {
        setLoading(false)
        if (!ok) {
          setMessage('')
        }
      }
    })()

    const timer = setInterval(() => {
      if (!isMounted || busyUserId != null) return
      refreshUsers()
    }, pollingIntervalMs)

    return () => {
      isMounted = false
      clearInterval(timer)
    }
  }, [pollingIntervalMs, busyUserId])

  function buildActionGuard(targetUser, action) {
    if (!targetUser) return 'Invalid user.'

    if (currentUserId != null && Number(targetUser.id) === currentUserId) {
      return 'You cannot modify your own role.'
    }

    if (targetUser.is_superuser) {
      return 'Superuser role changes are reserved for root administration.'
    }

    if (action === 'demote' && targetUser.is_staff && adminCount <= 1) {
      return 'Cannot remove the last remaining admin account.'
    }

    return ''
  }

  async function castRoleVote(targetUser, action) {
    const guardMessage = buildActionGuard(targetUser, action)
    if (guardMessage) {
      setError(guardMessage)
      setMessage('')
      return
    }

    setBusyUserId(targetUser.id)
    setError('')
    setMessage('')
    try {
      const res = await api.postJSON(`/api/admin/users/${targetUser.id}/${action}/`, {})
      if (res.ok && res.body) {
        setUsers(prev => prev.map(user => (user.id === targetUser.id ? { ...user, ...res.body } : user)))
        if (res.body.action_applied) {
          setMessage(`${action === 'promote' ? 'Promoted' : 'Demoted'} ${targetUser.username} after reaching the vote threshold.`)
        } else {
          setMessage(`Vote recorded for ${targetUser.username}.`)
        }
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        setError(detail || `Failed to ${action} user.`)
      }
    } catch (err) {
      setError(`Failed to ${action} user.`)
    } finally {
      setBusyUserId(null)
    }
  }

  async function approveUser(targetUser) {
    setBusyUserId(targetUser.id)
    setError('')
    setMessage('')
    try {
      const res = await api.postJSON(`/api/admin/users/${targetUser.id}/approve/`, {})
      if (res.ok && res.body) {
        setUsers(prev => prev.map(user => (user.id === targetUser.id ? { ...user, ...res.body, approval: { ...(user.approval || {}), is_approved: true } } : user)))
        setMessage(`Approved ${targetUser.username}.`)
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        setError(detail || 'Failed to approve user.')
      }
    } catch (err) {
      setError('Failed to approve user.')
    } finally {
      setBusyUserId(null)
    }
  }

  async function rejectUser(targetUser) {
    setBusyUserId(targetUser.id)
    setError('')
    setMessage('')
    try {
      const res = await api.postJSON(`/api/admin/users/${targetUser.id}/reject/`, {})
      if (res.ok) {
        setUsers(prev => prev.filter(user => user.id !== targetUser.id))
        setMessage(`Rejected and deleted ${targetUser.username}.`)
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        setError(detail || 'Failed to reject user.')
      }
    } catch (err) {
      setError('Failed to reject user.')
    } finally {
      setBusyUserId(null)
    }
  }

  async function deleteUser(targetUser) {
    const guardMessage = buildActionGuard(targetUser, 'demote')
    if (guardMessage) {
      setError(guardMessage)
      setMessage('')
      return
    }

    setBusyUserId(targetUser.id)
    setError('')
    setMessage('')
    try {
      const res = await api.deleteJSON(`/api/admin/users/${targetUser.id}/`)
      if (res.ok) {
        setUsers(prev => prev.filter(user => user.id !== targetUser.id))
        setMessage(`Deleted ${targetUser.username}.`)
      } else {
        const detail = res.body && (res.body.detail || JSON.stringify(res.body))
        setError(detail || 'Failed to delete user.')
      }
    } catch (err) {
      setError('Failed to delete user.')
    } finally {
      setBusyUserId(null)
    }
  }

  return (
    <section className="admin-users-page">
      <div className="admin-users-hero">
        <div>
          <h3>Admin User Management</h3>
          <p>Approve new accounts, then manage staff role changes through admin voting.</p>
        </div>
        <button type="button" className="admin-users-refresh" onClick={refreshUsers} disabled={loading || busyUserId != null}>
          Refresh
        </button>
      </div>

      {message ? <p className="admin-users-message">{message}</p> : null}
      {error ? <p className="admin-users-error">{error}</p> : null}

      <div className="admin-users-summary">
        <div>
          <span>Total users</span>
          <strong>{users.length}</strong>
        </div>
        <div>
          <span>Admins</span>
          <strong>{adminCount}</strong>
        </div>
      </div>

      {loading ? (
        <p className="admin-users-loading">Loading users...</p>
      ) : (
        <div className="admin-users-table-wrap">
          <table className="admin-users-table">
            <thead>
              <tr>
                <th>Username</th>
                <th>Email</th>
                <th>Role</th>
                <th>Status</th>
                <th>Approval</th>
                <th>Votes</th>
                <th>Actions</th>
              </tr>
            </thead>
            <tbody>
              {users.map(user => {
                const isSelf = currentUserId != null && Number(user.id) === currentUserId
                const busy = busyUserId === user.id
                const approvalState = getApprovalState(user)
                const voteSummary = getRelevantVoteSummary(user)
                const canVote = approvalState === 'approved' && !user.is_superuser && !isSelf

                return (
                  <tr key={user.id}>
                    <td>{user.username}</td>
                    <td>{user.email || '-'}</td>
                    <td>
                      <span className={`admin-users-role ${user.is_staff ? 'admin' : 'user'}`}>
                        {user.role || (user.is_staff ? 'ADMIN' : 'USER')}
                      </span>
                    </td>
                    <td>
                      {user.is_superuser ? 'Superuser' : user.is_active ? 'Active' : 'Inactive'}
                      {isSelf ? ' (You)' : ''}
                    </td>
                    <td>
                      {approvalState === 'approved' ? 'Approved' : 'Pending'}
                    </td>
                    <td>
                      {approvalState === 'approved' ? `${voteSummary.voteCount}/${voteSummary.threshold}` : '—'}
                    </td>
                    <td className="admin-users-actions">
                      {approvalState === 'approved' ? (
                        <>
                          <button
                            type="button"
                            className="admin-users-btn vote"
                            onClick={() => castRoleVote(user, voteSummary.voteType)}
                            disabled={busy || !canVote}
                          >
                            {busy ? 'Saving...' : `Vote ${voteSummary.voteType === 'promote' ? 'Promote' : 'Demote'}`}
                          </button>
                          <button
                            type="button"
                            className="admin-users-btn delete"
                            onClick={() => deleteUser(user)}
                            disabled={busy || isSelf || user.is_superuser}
                          >
                            {busy ? 'Saving...' : 'Delete'}
                          </button>
                        </>
                      ) : (
                        <>
                          <button
                            type="button"
                            className="admin-users-btn approve"
                            onClick={() => approveUser(user)}
                            disabled={busy || isSelf || user.is_superuser}
                          >
                            {busy ? 'Saving...' : 'Approve'}
                          </button>
                          <button
                            type="button"
                            className="admin-users-btn reject"
                            onClick={() => rejectUser(user)}
                            disabled={busy || isSelf || user.is_superuser}
                          >
                            {busy ? 'Saving...' : 'Reject'}
                          </button>
                        </>
                      )}
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        </div>
      )}
    </section>
  )
}
