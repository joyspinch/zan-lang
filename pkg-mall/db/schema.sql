-- pkg-mall schema (MySQL 8 / MariaDB).
-- This file is the single source of truth for the database shape: it is
-- applied through the standard library Migration runner at startup, and the
-- standard library Model.FromTable() then reflects every table back into a
-- Model so the application never carries a hand-written duplicate schema.

CREATE TABLE IF NOT EXISTS users (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(64) NOT NULL,
  password_salt VARCHAR(64) NOT NULL,
  password_hash VARCHAR(64) NOT NULL,
  role VARCHAR(24) NOT NULL DEFAULT 'user',
  points BIGINT NOT NULL DEFAULT 0,
  status VARCHAR(24) NOT NULL DEFAULT 'active',
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS developer_profiles (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  display_name VARCHAR(128) NOT NULL,
  bio TEXT,
  status VARCHAR(24) NOT NULL DEFAULT 'pending',
  review_note TEXT,
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS categories (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  slug VARCHAR(64) NOT NULL,
  name VARCHAR(128) NOT NULL,
  sort_order INT NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS products (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  developer_id BIGINT NOT NULL,
  category_id BIGINT NOT NULL,
  slug VARCHAR(128) NOT NULL,
  name VARCHAR(160) NOT NULL,
  description TEXT,
  status VARCHAR(24) NOT NULL DEFAULT 'draft',
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS product_versions (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  product_id BIGINT NOT NULL,
  version VARCHAR(64) NOT NULL,
  major_version INT NOT NULL,
  artifact_url TEXT NOT NULL,
  release_notes TEXT,
  status VARCHAR(24) NOT NULL DEFAULT 'pending',
  review_note TEXT,
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS pricing_plans (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  product_id BIGINT NOT NULL,
  name VARCHAR(128) NOT NULL,
  license_mode VARCHAR(24) NOT NULL,
  price_points BIGINT NOT NULL,
  duration_days INT NOT NULL DEFAULT 0,
  commission_percent INT NOT NULL DEFAULT 10,
  enabled INT NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS orders (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  order_no VARCHAR(64) NOT NULL,
  user_id BIGINT NOT NULL,
  product_id BIGINT NOT NULL,
  plan_id BIGINT NOT NULL,
  amount_points BIGINT NOT NULL,
  commission_points BIGINT NOT NULL,
  status VARCHAR(24) NOT NULL DEFAULT 'paid',
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS entitlements (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  product_id BIGINT NOT NULL,
  plan_id BIGINT NOT NULL,
  license_mode VARCHAR(24) NOT NULL,
  major_version INT NOT NULL DEFAULT 0,
  expires_at BIGINT NOT NULL DEFAULT 0,
  disabled INT NOT NULL DEFAULT 0,
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS point_ledger (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  delta BIGINT NOT NULL,
  reason VARCHAR(64) NOT NULL,
  reference_id VARCHAR(64),
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS developer_ledger (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  developer_id BIGINT NOT NULL,
  delta BIGINT NOT NULL,
  reason VARCHAR(64) NOT NULL,
  reference_id VARCHAR(64),
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS recharge_codes (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  code_digest VARCHAR(64) NOT NULL,
  points BIGINT NOT NULL,
  redeemed_by BIGINT,
  redeemed_at BIGINT NOT NULL DEFAULT 0,
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS reviews (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  product_id BIGINT NOT NULL,
  rating INT NOT NULL,
  content TEXT,
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS withdrawals (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  developer_id BIGINT NOT NULL,
  amount_points BIGINT NOT NULL,
  account VARCHAR(256) NOT NULL,
  status VARCHAR(24) NOT NULL DEFAULT 'pending',
  review_note TEXT,
  created_at BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS licenses (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  entitlement_id BIGINT NOT NULL,
  token_digest VARCHAR(64) NOT NULL,
  mode VARCHAR(24) NOT NULL,
  device_id VARCHAR(128),
  expires_at BIGINT NOT NULL,
  revoked INT NOT NULL DEFAULT 0,
  created_at BIGINT NOT NULL
);

CREATE UNIQUE INDEX ux_users_username ON users(username);
CREATE UNIQUE INDEX ux_developer_user ON developer_profiles(user_id);
CREATE UNIQUE INDEX ux_categories_slug ON categories(slug);
CREATE UNIQUE INDEX ux_products_slug ON products(slug);
CREATE UNIQUE INDEX ux_versions_product_version ON product_versions(product_id, version);
CREATE UNIQUE INDEX ux_orders_no ON orders(order_no);
CREATE UNIQUE INDEX ux_recharge_digest ON recharge_codes(code_digest);
CREATE UNIQUE INDEX ux_licenses_digest ON licenses(token_digest);
CREATE UNIQUE INDEX ux_reviews_user_product ON reviews(user_id, product_id);
CREATE INDEX ix_entitlements_user_product ON entitlements(user_id, product_id);
CREATE INDEX ix_versions_product_status ON product_versions(product_id, status);

INSERT IGNORE INTO categories(slug,name,sort_order) VALUES('system','System',0);
INSERT IGNORE INTO categories(slug,name,sort_order) VALUES('scripting','Scripting',10);
INSERT IGNORE INTO categories(slug,name,sort_order) VALUES('developer-tools','Developer Tools',20);

INSERT IGNORE INTO products(developer_id,category_id,slug,name,description,status,created_at)
  SELECT 0,id,'system-scripting-python','System.Scripting.Python','Official Python scripting package','approved',UNIX_TIMESTAMP()
  FROM categories WHERE slug='scripting';

INSERT IGNORE INTO product_versions(product_id,version,major_version,artifact_url,release_notes,status,created_at)
  SELECT id,'1.0.0',1,'packages/System.Scripting.Python-1.0.0.zpkg','Initial reviewed release','approved',UNIX_TIMESTAMP()
  FROM products WHERE slug='system-scripting-python';

INSERT INTO pricing_plans(product_id,name,license_mode,price_points,duration_days,commission_percent,enabled)
  SELECT id,'Free','once',0,0,0,1 FROM products p WHERE p.slug='system-scripting-python'
  AND NOT EXISTS(SELECT 1 FROM pricing_plans x WHERE x.product_id=p.id);
