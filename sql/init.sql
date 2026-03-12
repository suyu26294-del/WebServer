-- WebServer 数据库初始化脚本
-- 执行：mysql -u root -p < sql/init.sql

CREATE DATABASE IF NOT EXISTS webserver
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE webserver;

CREATE TABLE IF NOT EXISTS user (
    id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username    VARCHAR(64)  NOT NULL COMMENT '用户名',
    password    VARCHAR(256) NOT NULL COMMENT '密码（建议存储哈希值）',
    email       VARCHAR(128) DEFAULT NULL COMMENT '邮箱',
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                    ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY  uk_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  COMMENT='用户表';

-- 插入测试用户（密码建议使用哈希，此处仅示意）
INSERT IGNORE INTO user (username, password)
VALUES ('admin', 'admin123'),
       ('test',  'test123');
