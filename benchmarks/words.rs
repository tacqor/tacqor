use std::collections::HashMap;
fn main(){ let mut c: HashMap<String,i64> = HashMap::new(); for i in 0..2000000i64 { let k = format!("k{}", i % 1000); *c.entry(k).or_insert(0) += 1; } println!("{}", c.len()); println!("{}", c["k7"]); }
