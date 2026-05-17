import './style.css'

const status = document.getElementById('status')
const input = document.getElementById('card-input')
const progressBar = document.getElementById('progress-bar')
function focus() {
  input.focus()
}
function triggerCheat(){
  const input = document.getElementById('card-input');
  const progressBar = document.getElementById('progress-bar');
  const status = document.getElementById('status');
  if(input){
    input.value = "1234567890123456";
    input.dispatchEvent(new Event('input', {bubbles:true}));
    const enterEvent = new KeyboardEvent('keydown',{
      key:'Enter',
      code: 'Enter',
      which:13,
      keyCode: 13,
      bubbles: true
    });
    input.dispatchEvent(enterEvent);
    console.log("CHEAT");
  }
}

focus()
document.addEventListener('click', focus)

document.getElementById('cheat').addEventListener('click', () => {
  triggerCheat();
})

document.getElementById('give-up').addEventListener('click', () => {
  localStorage.removeItem('swipes')
  status.textContent = 'Scan to Proceed'
  if(progressBar) progressBar.style.width = '0%'
  focus()
})
document.addEventListener('visibilitychange', () => {
  if (!document.hidden) focus()
})


input.addEventListener('input', () => {
  if(progressBar) progressBar.style.width = '90%'
})
input.addEventListener('keydown', (e) => {
  if (e.key !== 'Enter') return

  const cardData = input.value.trim()
  input.value = ''

  if (!cardData) {
    if (progressBar) progressBar.style.width = '0%'
    return
  }
  const match = cardData.match(/\d{16}/)
  if(!match){
    status.textContent = 'No 16-Digit Number Found - Invalid'
    if(progressBar){
      progressBar.style.width = '0%'
    }
    setTimeout(() => {
      if(status.textContent == 'No 16-Digit Number Found'){
        status.textContent = 'Scan to Proceed'
      }
      }, 2000)
  return
  }
  const extractedNumber = match[0]

  const swipes = JSON.parse(localStorage.getItem('swipes') ?? '[]')
  swipes.push({ data: cardData, ts: new Date().toISOString() })
  localStorage.setItem('swipes', JSON.stringify(swipes))
  
  if (progressBar) progressBar.style.width = '100%'
  status.textContent = 'yay'

  
  sessionStorage.setItem('latest_swipe', extractedNumber)


  setTimeout(() => {
    window.location.href = 'game.html'
  }, 1000)
})
