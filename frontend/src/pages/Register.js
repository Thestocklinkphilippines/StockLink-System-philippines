import React, { useState } from 'react'
import { useNavigate, Link } from 'react-router-dom'
import api from '../api'
import '../styles/register.css'

export default function Register(){
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [show, setShow] = useState(false)
  const [error, setError] = useState('')
  const navigate = useNavigate()

  function EyeOffIcon() {
    return (
      <svg
        className="icon"
        fill="none"
        stroke="currentColor"
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeWidth="3"
        viewBox="0 0 24 24"
      >
        <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z" />
        <circle cx="12" cy="12" r="3" />
      </svg>
    );
  }

  function EyeIcon() {
    return (
      <svg 
        width="21px" 
        height="21px" 
        viewBox="0 0 24 24" 
        xmlns="http://www.w3.org/2000/svg">
        <g fill="none"
          fill-rule="evenodd" 
          stroke="currentColor" 
          stroke-linecap="round" 
          stroke-linejoin="round" 
          transform="translate(2 10)">   
          <path d="m0 .5c2.53705308 3.66666667 5.37038642 5.5 8.5 5.5 3.1296136 0 5.9629469-1.83333333 8.5-5.5"/>
          <path d="m2.5 3.423-2 2.077"/>
          <path d="m14.5 3.423 2 2.077"/>
          <path d="m10.5 6 1 2.5"/>
          <path d="m6.5 6-1 2.5"/>
        </g>
      </svg>
    );
  }

  async function submit(e){
    e.preventDefault()
    setError('')
    const res = await api.postJSON('/api/auth/register/', { username, password })
    if (res.ok) {
      navigate('/dashboard/overview')
    } else {
      setError(res.body && res.body.detail ? res.body.detail : 'Register failed')
    }
  }

  return (
<main>
	<main_content>
      <sidepanel>
		<div className="circle1"></div>
		<div className="circle2"></div>
		<div className="logo-container">
          <svg
            className="logo-icon"
            fill="none"
            stroke="currentColor"
            strokeLinecap="round"
            strokeLinejoin="round"
            strokeWidth="2"
            viewBox="0 0 24 24"
            xmlns="http://www.w3.org/2000/svg"
          >
            <path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5" />
          </svg>
        </div>

       	<h1 className="branding-title">StockLink IoT</h1>
                <p className="branding-text">Join the network of smart livestock management. Monitor, manage, and automate your farm with precision.</p>

				<features>

					<feature-item>
						<feature-icon>
							<svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth={3} viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
						</feature-icon>
						<feature-text>
							<span>Real-time Resource Monitoring</span>
						</feature-text>
					</feature-item>

					<feature-item>
						<feature-icon>
							<svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth={3} viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
						</feature-icon>
						<feature-text>
						<span>Automated Scheduling Engine</span>
						</feature-text>
					</feature-item>

					<feature-item>
						<feature-icon>
							<svg className="feature-svg" fill="none" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth={3} viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><polyline points="20 6 9 17 4 12"></polyline></svg>
						</feature-icon>
						<feature-text>
						<span>Instant Smart Alert System</span>
						</feature-text>
					</feature-item>

				</features>
	  </sidepanel>

	  <registerpanel>
		<h2 className="logintitle">Create Account</h2>
		<p className="logintitlemessage">Welcome! Please fill in the details below to register your farm management account.</p>
				<form onSubmit={submit}>
                    
					<usernamefield>
						<p>Username</p>
						<input type="text" id="username" name="username" required value={username} onChange={e=>setUsername(e.target.value)} />
					</usernamefield>
                    
					<emailfield>
						<p>Email Address</p>
						<input type="text" id="email" name="email" required/>
					</emailfield>

					<passwordfield>
						<div className="password-label-container">
							<label htmlFor="password">Password</label>
							<a className="forgot-link" href="#">Forgot Password?</a>
						</div>
							<input id="password" name="password" placeholder="••••••••" required type={show? 'text':'password'} value={password} onChange={e=>setPassword(e.target.value)} />
							<button type="button" className={`password-toggle ${show ? "active" : ""}`} onClick={() => setShow(!show)}>
			  {show ? <EyeOffIcon /> : <EyeIcon />}
			</button>
					</passwordfield>

					<div className="terms">
				<input id="terms-agreed" name="terms-agreed" type="checkbox"/>
				<label htmlFor="terms-agreed">
							I Agree to the <a href="#" className="terms-link">Terms of Service</a> and <a href="#" className="privacy-link">Privacy Policy</a>
						</label>
		  	</div>

					<button className="login-button" type="submit">
						<span className="login-button-text">Create Account</span>
						<svg className="login-button-arrow" xmlns="http://www.w3.org/2000/svg" width="3em" height="3em" viewBox="0 0 24 24" fill="none">
							<path d="M6 12H18M18 12L13 7M18 12L13 17" stroke="#000000" strokeWidth={2} strokeLinecap="round" strokeLinejoin="round"/>
						</svg>
					</button>

				</form>




				<footer class="footer-links">
        	<div class="createacc">
						<p>
							Already have an account?
							<a class="createacc-link" href="/login">
								Login here
							</a>
						</p>
					</div>

        	<div class="copyright">
        	  <p>© 2023 StockLink IoT Systems. All rights reserved. <br/>Secure poultry management platform.</p>
        	</div>
      	</footer>

      </registerpanel>

		</main_content>
	</main>
  )
}
