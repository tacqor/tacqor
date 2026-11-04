fn score(n: i64) -> i64 { if n >= 0 && n < 100000000 { (n * 48271 + 17) % 2147483647 } else { 0 } }
fn main(){ let mut t: i64 = 0; for i in 0..12000000i64 { t = t.checked_add(score(i)).expect("overflow"); } println!("{}", t); }
